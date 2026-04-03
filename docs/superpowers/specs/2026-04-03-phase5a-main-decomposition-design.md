# Phase 5a: main.cpp Decomposition — Design Specification

**Date:** 2026-04-03
**Status:** Draft
**Phase:** 5a (God Object Decomposition — main.cpp)
**Prerequisite:** Phase 4 complete (PR #7, `2e65afb`)

---

## 1. Goal

Decompose `reseq/main.cpp` (1,167 lines) from a monolithic file containing 4 inline
command implementations + 8 helper functions into a slim dispatcher (~80 lines) with
command logic extracted into focused files under `reseq/cli/`.

## 2. Current Architecture

### File Structure

```
reseq/main.cpp (1,167 lines)
  Lines 1-64:     Includes, globals (kVerbosityLevel, kNoDebugOutput), SeqAn statics
  Lines 66-82:    AutoDetectThreads() — used by replaceN, illuminaPE, seqToIllumina
  Lines 84-117:   DefaultExtensionFile() — used by GetDataStats
  Lines 119-291:  GetDataStats() — used by illuminaPE only
  Lines 293-300:  GetProbsOut() — used by PrepareProbabilityEstimation
  Lines 302-321:  PrepareProbabilityEstimation() — used by illuminaPE, seqToIllumina
  Lines 323-335:  GetSeed() — used by replaceN, WriteSysError, seqToIllumina
  Lines 337-388:  WriteSysError() — used by illuminaPE only
  Lines 390-405:  PrepareSimulation() — used by illuminaPE only
  Lines 408-453:  main() — general options, version, help, dispatch
  Lines 455-605:  queryProfile command (~150 lines)
  Lines 606-681:  replaceN command (~75 lines)
  Lines 682-1018: illuminaPE command (~337 lines, largest)
  Lines 1019-1153: seqToIllumina command (~135 lines)
  Lines 1154-1167: error handling, return
```

### Dispatch Mechanism

String-based if/else chain on `unrecognized_opts.at(0)`. Each command:
1. Prints mode if verbosity >= 3
2. Erases command name from unrecognized_opts
3. Creates per-command `options_description` + `usage_str`
4. Parses command-specific args
5. Checks `general_opts_map.count("help")` for help display
6. Executes command logic inline

### Shared State

- `kVerbosityLevel` (atomic global) — read everywhere for logging guards
- `kNoDebugOutput` (bool global) — defined but unused in main.cpp
- `num_threads` — parsed from general options, passed to each command
- `general_opts_map` — general options, used by commands for help check
- SeqAn complement functor statics (lines 57-63) — required for SeqAn initialization

### Helper Function Dependencies

| Helper | Used By | Placement |
|--------|---------|-----------|
| `AutoDetectThreads` | replaceN, illuminaPE, seqToIllumina | cli_common (shared) |
| `DefaultExtensionFile` | GetDataStats | cli_common (shared) |
| `GetSeed` | replaceN, WriteSysError, seqToIllumina | cli_common (shared) |
| `GetProbsOut` | PrepareProbabilityEstimation | cli_common (shared) |
| `PrepareProbabilityEstimation` | illuminaPE, seqToIllumina | cli_common (shared) |
| `GetDataStats` | illuminaPE only | illumina_pe (private) |
| `WriteSysError` | illuminaPE only | illumina_pe (private) |
| `PrepareSimulation` | illuminaPE only | illumina_pe (private) |

## 3. Target Architecture

### File Structure

```
reseq/main.cpp               (~80 lines)  — globals, general opts, dispatch calls
reseq/cli/cli_common.h       (~50 lines)  — shared function declarations, types
reseq/cli/cli_common.cpp     (~180 lines) — AutoDetectThreads, DefaultExtensionFile,
                                             GetSeed, GetProbsOut, PrepareProbabilityEstimation
reseq/cli/query_profile.h    (~15 lines)  — RunQueryProfile declaration
reseq/cli/query_profile.cpp  (~160 lines) — queryProfile implementation
reseq/cli/replace_n.h        (~15 lines)  — RunReplaceN declaration
reseq/cli/replace_n.cpp      (~85 lines)  — replaceN implementation
reseq/cli/illumina_pe.h      (~15 lines)  — RunIlluminaPE declaration
reseq/cli/illumina_pe.cpp    (~400 lines) — illuminaPE + GetDataStats, WriteSysError,
                                             PrepareSimulation (file-static helpers)
reseq/cli/seq_to_illumina.h  (~15 lines)  — RunSeqToIllumina declaration
reseq/cli/seq_to_illumina.cpp (~140 lines) — seqToIllumina implementation
```

### Command Interface

Each command is a free function in `reseq::cli` namespace:

```cpp
// reseq/cli/illumina_pe.h
#ifndef CLI_ILLUMINA_PE_H
#define CLI_ILLUMINA_PE_H

#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include "utilities.hpp"

namespace reseq::cli {

int RunIlluminaPE(const std::vector<std::string>& args,
                  uintNumThreads num_threads,
                  const boost::program_options::variables_map& general_opts,
                  boost::program_options::options_description& opt_desc_full);

} // namespace reseq::cli

#endif
```

Each command function:
1. Creates its own `options_description` and `usage_str`
2. Parses `args` with Boost.program_options
3. Checks `general_opts.count("help")` for help display
4. Adds its options to `opt_desc_full` (for combined help output)
5. Executes command logic
6. Returns 0 on success, 1 on error

### main.cpp After Extraction

```cpp
#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#include <boost/program_options.hpp>

#include "logging.hpp"
#include "CMakeConfig.h"
#include "utilities.hpp"

#include "cli/query_profile.h"
#include "cli/replace_n.h"
#include "cli/illumina_pe.h"
#include "cli/seq_to_illumina.h"

// SeqAn complement functor statics (required for initialization)
// ... existing lines 57-63 ...

namespace reseq {
std::atomic<uint16_t> kVerbosityLevel{99};
bool kNoDebugOutput = false;
} // namespace reseq

int main(int argc, const char* argv[]) {
    using namespace boost::program_options;
    using namespace reseq;

    uint16_t verbosity_opt = 4;
    uintNumThreads num_threads = 0;

    options_description opt_desc_full("General");
    opt_desc_full.add_options()
        ("help,h", "Display this help message")
        ("threads,j", value<uintNumThreads>(&num_threads)->default_value(0), "...")
        ("verbosity", value<uint16_t>(&verbosity_opt)->default_value(4), "...")
        ("version", "Display version");

    // Parse general options, allow unrecognized (command + command args)
    auto parsed = command_line_parser(argc, argv)
        .options(opt_desc_full).allow_unregistered().run();
    variables_map general_opts_map;
    store(parsed, general_opts_map);
    notify(general_opts_map);
    kVerbosityLevel.store(verbosity_opt);

    auto unrecognized_opts = collect_unrecognized(parsed.options, include_positional);

    if (general_opts_map.count("version")) {
        std::cerr << "ReSeq version " << RESEQ_VERSION_MAJOR << '.'
                  << RESEQ_VERSION_MINOR << std::endl;
        return 0;
    }

    if (unrecognized_opts.empty()) {
        // Print general usage
        std::string general_usage = "...";
        std::cerr << general_usage << opt_desc_full << std::endl;
        return general_opts_map.count("help") ? 0 : 1;
    }

    // Version banner
    if (2 < kVerbosityLevel) {
        std::cerr << "ReSeq " << RESEQ_GIT_VERSION << std::endl;
    }

    std::string cmd = unrecognized_opts.at(0);
    unrecognized_opts.erase(unrecognized_opts.begin());

    int ret = 1;
    if (cmd == "queryProfile")
        ret = cli::RunQueryProfile(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
    else if (cmd == "replaceN")
        ret = cli::RunReplaceN(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
    else if (cmd == "illuminaPE")
        ret = cli::RunIlluminaPE(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
    else if (cmd == "seqToIllumina")
        ret = cli::RunSeqToIllumina(unrecognized_opts, num_threads, general_opts_map, opt_desc_full);
    else {
        printErr << "'" << cmd << "' is not a valid command." << std::endl;
        // ... print usage ...
    }

    return ret;
}
```

### Shared Helpers (cli_common)

```cpp
// reseq/cli/cli_common.h
namespace reseq::cli {

void AutoDetectThreads(uintNumThreads& num_threads);

std::string DefaultExtensionFile(const std::string& stats_in,
                                  const std::string& extension,
                                  const std::string& adapter_dir);

uintSeed GetSeed(const boost::program_options::variables_map& opts_map);

std::string GetProbsOut(const std::string& stats_in,
                        const std::string& probs_file);

bool PrepareProbabilityEstimation(std::string& probs_in,
                                   std::string& probs_out,
                                   const boost::program_options::variables_map& opts_map,
                                   const std::string& stats_in);

} // namespace reseq::cli
```

### CMake Changes

Add new source files to `reseq_lib` in `reseq/CMakeLists.txt`. The `cli/` directory
is under `reseq/` which is already in `target_include_directories`, so `#include "cli/foo.h"`
works.

Note: The command files are added to `reseq_lib` (not the `reseq` executable) so that
they can potentially be tested. `main.cpp` remains the only file in the `reseq` executable
target.

```cmake
add_library(reseq_lib STATIC
  ...existing files...
  cli/cli_common.cpp
  cli/query_profile.cpp
  cli/replace_n.cpp
  cli/illumina_pe.cpp
  cli/seq_to_illumina.cpp
)
```

## 4. Migration Strategy

Follow the facade + forwarding pattern from the research:

1. **Create cli/ directory and cli_common** with shared helpers extracted from main.cpp.
   main.cpp calls the helpers from cli_common. Tests pass.

2. **Extract one command at a time** (simplest first): replaceN → queryProfile →
   seqToIllumina → illuminaPE. For each:
   - Create `cli/<command>.h` and `cli/<command>.cpp`
   - Move command logic into `Run<Command>()` function
   - Replace inline code in main.cpp with function call
   - Build and test

3. **Clean up main.cpp** — remove dead includes, format.

Each step is one commit. Tests pass at every step.

## 5. Invariants

| # | Invariant | How Preserved |
|---|-----------|---------------|
| M1 | CLI behavior identical | Golden-file regression tests verify byte-identical output |
| M2 | Help/version output unchanged | Regression tests for --version and error commands |
| M3 | Exit codes preserved | Each Run function returns same int as original code path |
| M4 | Global state initialization order | kVerbosityLevel set before any command runs (stays in main) |
| M5 | SeqAn static initialization | Complement functor statics remain in main.cpp translation unit |

## 6. Testing

### Existing Coverage

The 10 golden-file regression tests exercise all 4 commands:
- `ReplaceN` — replaceN command
- `QueryProfileMaxReadLength`, `QueryProfileMaxLenDeletion`, `QueryProfileFragLenBias` — queryProfile
- `RealProfileMaxReadLength`, `RealProfileMaxLenDeletion`, `RealProfileFragLenBias` — queryProfile on real data
- `ErrorBadCommand`, `ErrorMissingRef` — error handling
- `VersionOutput` — version display

These are subprocess-based tests that invoke the `reseq` binary — they test the full
CLI pipeline regardless of internal file structure. They are the primary safety net.

### No New Unit Tests Needed

This is a mechanical extraction (moving code between files, no logic changes). The
regression tests provide complete coverage of all command paths. New unit tests would
only be needed if we changed behavior, which we explicitly do not.

## 7. Verification Criteria

- `make build && make test` — all unit tests + regression tests pass at every commit
- `make format-check` — no formatting issues
- `wc -l reseq/main.cpp` — target ~80 lines after extraction
- Binary output identical: run each command before and after, diff output
