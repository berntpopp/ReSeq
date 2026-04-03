#include <algorithm>
using std::count;
using std::max;
#include <atomic>
#include <exception>
using std::exception;
#include <iostream>
using std::cerr;
using std::cout;
#include <stdint.h>
#include <stdio.h>
#include <string>
using std::string;
using std::to_string;
#include <sys/stat.h>
#include <thread>
#include <vector>
using std::vector;

namespace reseq {
std::atomic<uint16_t> kVerbosityLevel{99};
bool kNoDebugOutput = false;
} // namespace reseq
#include "logging.hpp"
using reseq::kVerbosityLevel;

#include <boost/program_options.hpp>
using boost::program_options::command_line_parser;
using boost::program_options::include_positional;
using boost::program_options::notify;
using boost::program_options::options_description;
using boost::program_options::parsed_options;
using boost::program_options::store;
using boost::program_options::value;
using boost::program_options::variable_value;
using boost::program_options::variables_map;

#include "cli/cli_common.h"
#include "cli/illumina_pe.h"
#include "cli/query_profile.h"
#include "cli/replace_n.h"
#include "cli/seq_to_illumina.h"
#include "CMakeConfig.h"
#include "DataStats.h"
using reseq::DataStats;
#include "FragmentDistributionStats.h"
using reseq::RefSeqBiasSimulation;
#include "ProbabilityEstimates.h"
using reseq::ProbabilityEstimates;
#include "Reference.h"
using reseq::Reference;
#include "Simulator.h"
using reseq::Simulator;
#include "utilities.hpp"
using reseq::uintFragCount;
using reseq::uintNumFits;
using reseq::uintNumThreads;
using reseq::uintQualPrint;
using reseq::uintSeed;
using reseq::uintSeqLen;
using reseq::utilities::DeleteFile;
using reseq::utilities::FileExists;
using reseq::utilities::GetReSeqDir;
using reseq::utilities::TrueRandom;
using reseq::cli::AutoDetectThreads;
using reseq::cli::GetProbsOut;
using reseq::cli::GetSeed;
using reseq::cli::PrepareProbabilityEstimation;

// Definitions so that referencing a const static is valid
seqan::FunctorComplement<seqan::Dna5> reseq::utilities::Complement::Dna5;
seqan::FunctorComplement<seqan::Dna> reseq::utilities::Complement::Dna;

// Helper functions
bool DefaultExtensionFile(string& file_name, const string extension) {
    if (FileExists(file_name)) {
        // File exists without modification
        return true;
    }

    if (0 == count(file_name.begin(), file_name.end(), '.')) {
        file_name += extension;
        if (FileExists(file_name)) {
            // File exists with adding extension
            return true;
        }
    }

    if (0 == count(file_name.begin(), file_name.end(), '/')) {
        string adapter_dir;
        if (GetReSeqDir(adapter_dir, "adapters", "TruSeq_single.fa")) {
            file_name = adapter_dir + file_name;
        } else {
            file_name = "";
            return false;
        }

        if (FileExists(file_name)) {
            // File exists with adding folder
            return true;
        }
    }

    printErr << "Automatic filename deduction failed for: '" << file_name << "'. Please provide valid path."
             << std::endl;
    file_name = "";
    return false;
}

void GetDataStats(DataStats& real_data_stats, string& stats_file, bool& loaded_stats, bool stats_only,
                  uintSeqLen max_ref_seq_bin_size, const variables_map& opts_map, const options_description& opt_desc,
                  const string& usage_str, uintNumThreads num_threads) {
    auto it_bam_in = opts_map.find("bamIn");
    auto it_stats_in = opts_map.find("statsIn");
    auto it_stats_out = opts_map.find("statsOut");
    bool no_tiles = opts_map.count("noTiles");
    bool no_bias_calculation = opts_map.count("noBias");
    bool tiles = opts_map.count("tiles");
    if (no_tiles && tiles) {
        printErr << "noTiles and tiles options are exclusive." << std::endl;
        if (0 < kVerbosityLevel) {
            cerr << usage_str;
            cerr << opt_desc << std::endl;
        }
    }

    if (opts_map.end() == it_bam_in) { // "bamIn" hasn't been found
        if (stats_only) {
            printErr << "With statsOnly option bamIn option is mandatory." << std::endl;
            if (0 < kVerbosityLevel) {
                cerr << usage_str;
                cerr << opt_desc << std::endl;
            }
        } else if (no_tiles || tiles) {
            printErr << (no_tiles ? "noTiles" : "tiles")
                     << " option can only be specified for generation of statistics. After that it depends on the "
                        "statistics loaded."
                     << std::endl;
            if (0 < kVerbosityLevel) {
                cerr << usage_str;
                cerr << opt_desc << std::endl;
            }
        } else if (opts_map.end() == it_stats_in) { // "statsIn" hasn't been found
            printErr << "Either bamIn or statsIn option are mandatory." << std::endl;
            if (0 < kVerbosityLevel) {
                cerr << usage_str;
                cerr << opt_desc << std::endl;
            }
        } else {
            if (opts_map.end() != it_stats_out) {
                printWarn << "statsOut option ignored as statsIn is also given." << std::endl;
            }

            stats_file = it_stats_in->second.as<string>();
            printInfo << "Reading real data statistics from " << stats_file << std::endl;

            if (real_data_stats.Load(stats_file.c_str())) {
                if (no_bias_calculation) {
                    real_data_stats.SetUniformBias();
                }
                real_data_stats.PrepareProcessing();

                loaded_stats = true;
            }
        }
    } else {
        if (opts_map.end() != it_stats_in) {
            printErr << "statsIn and bamIn options are exclusive." << std::endl;
        } else if (!real_data_stats.HasReference()) {
            printErr << "Reading in statistics requires refIn option." << std::endl;
        } else {
            string bam_input(it_bam_in->second.as<string>());
            printInfo << "Reading mapping from " << bam_input << std::endl;

            if (opts_map.end() == it_stats_out) {
                stats_file = bam_input + ".reseq";
            } else {
                stats_file = it_stats_out->second.as<string>();
            }
            printInfo << "Storing real data statistics in " << stats_file << std::endl;

            // Set adapter files to input or default
            bool adapters_failed = false;
            string adapter_file, adapter_matrix;
            auto it_adapter_file = opts_map.find("adapterFile");
            auto it_adapter_matrix = opts_map.find("adapterMatrix");

            if (opts_map.end() != it_adapter_file) {
                adapter_file = it_adapter_file->second.as<string>();
                if (!DefaultExtensionFile(adapter_file, ".fa")) {
                    adapters_failed = true;
                }
            }
            if (opts_map.end() != it_adapter_matrix) {
                adapter_matrix = it_adapter_matrix->second.as<string>();
                if (!DefaultExtensionFile(adapter_matrix, ".mat")) {
                    adapters_failed = true;
                }
            }

            if (!adapters_failed) {
                if (adapter_file.size()) {
                    if (0 == adapter_matrix.size()) {
                        // Only sequences
                        if (2 < adapter_file.size()) {
                            adapter_matrix = adapter_file;
                            adapter_matrix.replace(adapter_matrix.size() - 3, 3, ".mat");
                            if (!FileExists(adapter_matrix)) {
                                adapters_failed = true;
                            }
                        } else {
                            adapters_failed = true;
                        }

                        if (adapters_failed) {
                            printErr << "Switching extension of adapter-sequence file did not result in valid matrix "
                                        "file. Please specify the matrix file."
                                     << std::endl;
                            if (0 < kVerbosityLevel) {
                                cerr << usage_str;
                                cerr << opt_desc << std::endl;
                            }
                        }
                    }
                } else if (adapter_matrix.size()) {
                    // Only matrix
                    if (3 < adapter_matrix.size()) {
                        adapter_file = adapter_matrix;
                        adapter_file.replace(adapter_file.size() - 4, 4, ".fa");
                        if (!FileExists(adapter_file)) {
                            adapters_failed = true;
                        }
                    } else {
                        adapters_failed = true;
                    }

                    if (adapters_failed) {
                        printErr << "Switching extension of adapter-matrix file did not result in valid sequence file. "
                                    "Please specify the sequence file."
                                 << std::endl;
                        if (0 < kVerbosityLevel) {
                            cerr << usage_str;
                            cerr << opt_desc << std::endl;
                        }
                    }
                }
            }

            if (!adapters_failed) {
                if (!tiles) {
                    printInfo << "Tiles will be ignored and all statistics will be generated like all reads are from "
                                 "the same tile."
                              << std::endl;
                    real_data_stats.IgnoreTiles();
                } else {
                    printInfo << "Statistics will be split into tiles if possible." << std::endl;
                }

                string variant_file = "";
                auto it_variant_file = opts_map.find("vcfIn");
                if (opts_map.end() != it_variant_file) {
                    variant_file = it_variant_file->second.as<string>();
                    printInfo << "Ignoring all variant positions listed in '" << variant_file
                              << "' for error statistics." << std::endl;
                }

                if (real_data_stats.ReadBam(bam_input.c_str(), adapter_file.c_str(), adapter_matrix.c_str(),
                                            variant_file, max_ref_seq_bin_size, num_threads, !no_bias_calculation)) {
                    real_data_stats.Save(stats_file.c_str());
                    real_data_stats.PrepareProcessing();
                    real_data_stats.ClearReference(); // It shouldn't be used after the read in to guarantee that we can
                                                      // remove or change the reference
                } else {
                    DeleteFile(
                        stats_file.c_str()); // Remove the stats file if one existed previously so it is clear that we
                                             // encountered an error and do not accidentally continue with the old file
                }
                loaded_stats = false;
            }
        }
    }
}

bool WriteSysError(string& sys_error_file, uintSeed& seed, bool stop_after_estimation, const variables_map& opts_map,
                   const options_description& opt_desc, const string& usage_str, const Reference& ref,
                   const DataStats& stats, const ProbabilityEstimates& estimates) {
    auto it_read = opts_map.find("readSysError");
    auto it_write = opts_map.find("writeSysError");

    if (opts_map.end() == it_write) {
        if (stop_after_estimation) {
            if (opts_map.end() != it_read) {
                printWarn << "readSysError option is only for simulation, so it's useless with the stopAfterEstimation "
                             "option."
                          << std::endl;
                if (0 < kVerbosityLevel) {
                    cerr << usage_str;
                    cerr << opt_desc << std::endl;
                }
                return false;
            }
        } else {
            if (opts_map.end() != it_read) {
                sys_error_file = it_read->second.as<string>();
            } else {
                sys_error_file = "";
            }

            seed = GetSeed(opts_map);
        }
    } else {
        if (opts_map.end() != it_read) {
            printErr << "writeSysError and readSysError option are mutually exclusive. Specify the one or the other."
                     << std::endl;
            if (0 < kVerbosityLevel) {
                cerr << usage_str;
                cerr << opt_desc << std::endl;
            }
            return false;
        } else {
            seed = GetSeed(opts_map);

            sys_error_file = it_write->second.as<string>();
            Simulator sim;
            if (!sim.CreateSystematicErrorProfile(sys_error_file.c_str(), ref, stats, estimates, seed)) {
                DeleteFile(sys_error_file
                               .c_str()); // Remove the sys error file if one existed previously so it is clear that we
                                          // encountered an error and do not accidentally continue with the old file
                return false;
            }
        }
    }

    return true;
}

void PrepareSimulation(string& sim_output_first, string& sim_output_second, const variables_map& opts_map) {
    auto it = opts_map.find("firstReadsOut");
    if (opts_map.end() == it) {
        sim_output_first = "reseq-R1.fq";
    } else {
        sim_output_first = it->second.as<string>();
    }

    it = opts_map.find("secondReadsOut");
    if (opts_map.end() == it) {
        sim_output_second = "reseq-R2.fq";
    } else {
        sim_output_second = it->second.as<string>();
    }
    printInfo << "Storing simulated data in " << sim_output_first << " and " << sim_output_second << std::endl;
}

// Main
int main(int argc, char* argv[]) {
    uintNumThreads num_threads;
    uint16_t verbosity_opt = 4;
    options_description opt_desc_full("General");
    opt_desc_full.add_options() // Returns a special object with defined operator ()
        ("help,h", "Prints help information and exits")(
            "threads,j", value<uintNumThreads>(&num_threads)->default_value(0), "Number of threads used (0=auto)")(
            "verbosity", value<uint16_t>(&verbosity_opt)->default_value(4),
            "Sets the level of verbosity (4=everything, 0=nothing)")("version", "Prints version info and exits");

    vector<string> unrecognized_opts;
    variables_map general_opts_map;
    try {
        parsed_options general_opts = command_line_parser(argc, argv).options(opt_desc_full).allow_unregistered().run();
        unrecognized_opts = collect_unrecognized(general_opts.options, include_positional);

        store(general_opts, general_opts_map);
        notify(general_opts_map);
        kVerbosityLevel.store(verbosity_opt);
    } catch (const exception& e) {
        printErr << "Could not parse general command line arguments: " << e.what() << std::endl;
        if (0 < kVerbosityLevel) {
            cerr << opt_desc_full << std::endl;
        }
        return 1;
    }

    if (general_opts_map.count("version")) { // Check if user only wants to know version
        cerr << "ReSeq version " << RESEQ_VERSION_MAJOR << '.' << RESEQ_VERSION_MINOR << std::endl;
        return 0;
    }

    string general_usage =
        string("\nProgram: reseq (REal SEQuence replicator)\n") + "Version: " + to_string(RESEQ_VERSION_MAJOR) + '.' +
        to_string(RESEQ_VERSION_MINOR) + '\n' + "Contact: Stephan Schmeing <stephan.schmeing@uzh.ch>\n\n" +
        "Usage:  reseq <command> [options]\n" + "Commands:\n" + "  illuminaPE\t\t" +
        "simulates illumina paired-end data\n" + "  queryProfile\t\t" +
        "queries reseq statistic files for information\n" + "  replaceN\t\t" + "replaces N's in reference\n" +
        "  seqToIllumina\t\t" + "applies illumina quality and error model to input sequences\n";

    int return_code = 0;
    if (0 == unrecognized_opts.size()) {
        cerr << general_usage << std::endl;
    } else {
        printInfo << "Running ReSeq version " << RESEQ_VERSION_MAJOR << '.'
                  << RESEQ_VERSION_MINOR; // Always show version

        if ("queryProfile" == unrecognized_opts.at(0)) {
            if (2 < kVerbosityLevel) {
                cerr << " in queryProfile mode" << std::endl;
            }
            unrecognized_opts.erase(unrecognized_opts.begin());
            return_code =
                reseq::cli::RunQueryProfile(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
        } else if ("replaceN" == unrecognized_opts.at(0)) {
            if (2 < kVerbosityLevel) {
                cerr << " in replaceN mode" << std::endl;
            }
            unrecognized_opts.erase(unrecognized_opts.begin());
            return_code =
                reseq::cli::RunReplaceN(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
        } else if ("illuminaPE" == unrecognized_opts.at(0)) {
            if (2 < kVerbosityLevel) {
                cerr << " in illuminaPE mode" << std::endl;
            }
            unrecognized_opts.erase(unrecognized_opts.begin());
            return_code =
                reseq::cli::RunIlluminaPE(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
        } else if ("seqToIllumina" == unrecognized_opts.at(0)) {
            if (2 < kVerbosityLevel) {
                cerr << " in seqToIllumina mode" << std::endl;
            }
            unrecognized_opts.erase(unrecognized_opts.begin());
            return_code =
                reseq::cli::RunSeqToIllumina(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
        } else {
            if (2 < kVerbosityLevel) {
                cerr << std::endl;
            }
            printErr << "Unrecognized command: '" << unrecognized_opts.at(0) << "'" << std::endl;
            if (0 < kVerbosityLevel) {
                cerr << general_usage << std::endl;
            }
            return 1;
        }
    }

    return return_code;
}
