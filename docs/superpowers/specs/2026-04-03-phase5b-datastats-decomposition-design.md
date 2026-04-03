# Phase 5b: DataStats Decomposition — Design Specification

**Date:** 2026-04-03
**Status:** Draft
**Scope:** Decompose the `DataStats` god object (1,844 lines) into focused, single-responsibility components
**Prerequisite:** Phase 5a (main.cpp decomposition) — merged
**Input:** Re-evaluation of original Phase 5 design against current codebase (post-Phases 3, 4, 6)

---

## Goal

Break `DataStats` into three components:
1. **ReadSequenceStats** — per-read histogram statistics (GC, N-content, homopolymers, mapping quality, sequence content)
2. **BamIngestionEngine** — multi-threaded two-pass BAM reading pipeline with pair-matching, record evaluation, and validation
3. **DataStats** (thinned) — aggregator that owns sub-stats, exposes getters, handles serialization and lifecycle

The public API of `DataStats` does not change. All callers continue to use it identically.

---

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| RecordClassifier extraction | No — keep predicates in BamIngestionEngine | Small inline predicates only called from within the BAM loop; separate class adds indirection without value |
| ReadSequenceStats scope | Histograms + atomics + EvalBaseLevelStats | This data group is fully independent of coverage, errors, adapters, and fragment distribution |
| BamIngestionEngine ownership model | Receives sub-stats by reference, does not own them | Avoids moving ownership; keeps sub-stat lifecycle under DataStats |
| BamFileMetadata struct | No — keep scalars in DataStats | Only 6 scalar fields; a struct adds a layer without meaningful encapsulation benefit |
| Public API stability | No changes to DataStats public interface | All callers (CLI commands, Simulator, ProbabilityEstimates, tests) work unchanged |
| Friend access | BamIngestionEngine is friend of DataStats | Necessary during transition; can be narrowed in Phase 6 (interface cleanup) |
| Test files | No new test files | Existing DataStatsTest.cpp exercises all paths; dedicated unit tests deferred to Phase 7 |

---

## Component 1: ReadSequenceStats

### Responsibility

Accumulates per-read sequence-level statistics during the BAM scan and exposes them as histograms afterward.

### Data Members (moved from DataStats)

**Atomic accumulators (thread-safe, filled during multi-threaded BAM pass):**
- `tmp_proper_pair_mapping_quality_` — `std::vector<utilities::VectorAtomic<uintFragCount>>`
- `tmp_improper_pair_mapping_quality_` — same type
- `tmp_single_read_mapping_quality_` — same type
- `tmp_gc_read_content_` — `std::array<std::vector<utilities::VectorAtomic<uintFragCount>>, 2>`
- `tmp_gc_read_content_reference_` — same type
- `tmp_gc_read_content_mapped_` — same type
- `tmp_n_content_` — same type
- `tmp_sequence_content_` — `std::array<std::array<std::vector<utilities::VectorAtomic<uintNucCount>>, 5>, 2>`
- `tmp_sequence_content_reference_` — `std::array<std::array<std::array<std::vector<utilities::VectorAtomic<uintNucCount>>, 4>, 2>, 2>`
- `tmp_homopolymer_distribution_` — `std::array<std::vector<utilities::VectorAtomic<uintNucCount>>, 5>`

**Final histograms (serialized):**
- `proper_pair_mapping_quality_` — `Vect<uintFragCount>`
- `improper_pair_mapping_quality_` — same type
- `single_read_mapping_quality_` — same type
- `gc_read_content_` — `std::array<Vect<uintFragCount>, 2>`
- `gc_read_content_reference_` — same type
- `gc_read_content_mapped_` — same type
- `n_content_` — same type
- `sequence_content_` — `std::array<std::array<Vect<uintNucCount>, 5>, 2>`
- `sequence_content_reference_` — `std::array<std::array<std::array<Vect<uintNucCount>, 4>, 2>, 2>`
- `homopolymer_distribution_` — `std::array<Vect<uintNucCount>, 5>`

### Methods (moved from DataStats)

- `PrepareAccumulators(max_read_len, max_qual, phred_offset)` — allocates all `tmp_*` vectors to correct sizes. Extracted from the relevant parts of `DataStats::PrepareReadIn`.
- `EvalBaseLevelStats(record, ref_seq_id, reference, template_segment, quality_offset)` — moved from `DataStats::EvalBaseLevelStats`. The only producer of all histogram data. Reads from a BAM record and reference, writes only to `tmp_*` members.
- `Finalize()` — copies atomic accumulators into final `Vect` structures. Extracted from the relevant parts of `DataStats::FinishReadIn`.
- `Shrink()` — trims histogram ranges. Extracted from `DataStats::Shrink`.

### Const Getters

Same signatures as current DataStats getters:
- `ProperPairMappingQuality()`, `ImproperPairMappingQuality()`, `SingleReadMappingQuality()`
- `GCReadContent(template_segment)`, `GCReadContentReference(template_segment)`, `GCReadContentMapped(template_segment)`
- `NContent(template_segment)`
- `SequenceContent(template_segment, nucleotide)`, `SequenceContentReference(template_segment, strand, nucleotide)`
- `HomopolymerDistribution(nucleotide)`

### Serialization

Boost `serialize()` template covering all final histogram members. Called as part of DataStats's own serialization (DataStats serializes `read_sequence_stats_` as a member).

### Dependencies

- `Vect<T>` (histogram container)
- `utilities::VectorAtomic<T>` (thread-safe accumulation)
- `seqan::BamAlignmentRecord` (read from)
- `Reference` (const ref for GC/N/surrounding lookups)

### Why This Seam Is Clean

`EvalBaseLevelStats` reads from a BAM record and reference sequence, writes only to its own `tmp_*` members. No interaction with coverage blocks, error stats, adapter detection, fragment distribution, or any other sub-stat. The method can be moved with a clean cut.

---

## Component 2: BamIngestionEngine

### Responsibility

Executes the two-pass BAM reading pipeline: pre-run (pass 1 for sizing), multi-threaded evaluation (pass 2), and finalization. Populates a DataStats object and its sub-stats by reference.

### Data Members (moved from DataStats)

**Threading primitives:**
- `read_mutex_`, `print_mutex_` — `std::mutex`
- `running_threads_` — `std::atomic<uintNumThreads>`
- `finish_threads_mutex_` — `std::mutex`
- `finish_threads_cv_` — `std::condition_variable`
- `finish_threads_` — `bool`

**Pair-matching state:**
- `first_read_records_` — `std::unordered_set<CoverageStats::FullRecord*, RecordHasher, RecordEqual>`
- `reading_success_` — `std::atomic<bool>`
- `read_records_` — `uintFragCount`
- `kBatchSize` — `const uintFragCount` (10000)

**Pre-run temporaries:**
- `reads_per_frag_len_bin_` — `std::vector<uintFragCount>`
- `lowq_reads_per_frag_len_bin_` — `std::vector<uintFragCount>`

**Read-disposition counters:**
- `reads_in_unmapped_pairs_without_adapters_` — `std::atomic<uintFragCount>`
- `reads_in_unmapped_pairs_with_adapters_` — same type
- `reads_with_low_quality_with_adapters_` — same type
- `reads_with_low_quality_without_adapters_` — same type
- `reads_on_too_short_fragments_` — same type
- `reads_in_excluded_regions_` — same type
- `reads_used_` — same type

### Methods (moved from DataStats)

**Pipeline orchestration:**
- `Run(bam_file, adapter_file, adapter_matrix, variant_file, max_ref_seq_bin_size, num_threads, calculate_bias, DataStats& target)` — replaces `DataStats::ReadBam` as the implementation. Opens BAM, runs PreRun, spawns threads, finalizes.
- `PreRun(bam, bam_file, header, ...)` — first pass: read lengths, quality range, adapter evidence, tile enumeration
- `ReadRecords(bam, target, ...)` — locked batch reader; pairs reads, registers with CoverageStats blocks
- `ReadThread(engine, bam, thread_id)` — static worker entry point
- `PrepareReadIn(target, ...)` — allocates all temporary arrays on DataStats and sub-stats
- `FinishReadIn(target)` — copies atomics to finals, finalizes sub-stats
- `Shrink(target)` — trims all histogram arrays
- `Calculate(target, num_threads)` — triggers bias calculation and computes corrected coverage

**Per-record evaluation:**
- `EvalRecord(...)` — dispatches one read pair through filters and statistics
- `EvalReferenceStatistics(...)` — CIGAR walk: reference GC, sequence content vs reference, errors, indels
- `CheckForAdapters(...)` — detects adapter contamination in unmapped/low-quality pairs
- `IsSecondRead(...)` — uses hash set to match mates

**Validation predicates (all inline):**
- `IsValidRecord(record)` — paired, consistent lengths, has sequence
- `QualitySufficient(record)` — mapQ threshold
- `PotentiallyValidGeneral(...)`, `PotentiallyValidFirst(...)`, `PotentiallyValidSecond(...)`, `PotentiallyValid(...)` — multi-condition filters
- `InProperDirection(...)` — FR orientation and insert-length check
- `GetReadLengthOnReference(record)` (x2) — CIGAR-corrected length
- `GetReadPosOnReference(start, end, record)` — soft-clip-corrected positions

**Cleanup:**
- `OrderOfBamFileCorrect(...)` — validates position-sort order
- `SignsOfPairsWithNamesNotIdentical(...)` — detects missing mates

### Interface

```cpp
class BamIngestionEngine {
public:
    bool Run(const char* bam_file, const char* adapter_file,
             const char* adapter_matrix, const std::string& variant_file,
             uintSeqLen max_ref_seq_bin_size, uintNumThreads num_threads,
             bool calculate_bias, DataStats& target);

    // Static geometry utilities (remain public as today)
    static uintReadLen GetReadLengthOnReference(const seqan::BamAlignmentRecord& record);
    static uintReadLen GetReadLengthOnReference(const seqan::BamAlignmentRecord& record,
                                                 uintReadLen& max_indel);
};
```

All other methods are private. The engine is constructed on the stack inside `DataStats::ReadBam`, runs, and is destroyed.

**Note:** `GetReadLengthOnReference` and `GetReadPosOnReference` are also called by `Simulator`. To avoid breaking callers, DataStats retains static forwarding methods that delegate to BamIngestionEngine's implementations. Alternatively, these geometry utilities could become free functions in a shared header — but that is deferred to Phase 6 (interface cleanup).

### Dependencies

- `DataStats` (friend, writes to target's members and sub-stats)
- `ReadSequenceStats` (calls `EvalBaseLevelStats`, `PrepareAccumulators`, `Finalize`, `Shrink`)
- All sub-stats classes via DataStats: `AdapterStats`, `CoverageStats`, `ErrorStats`, `FragmentDistributionStats`, `FragmentDuplicationStats`, `QualityStats`, `TileStats`
- `Reference` (via DataStats's reference pointer)
- SeqAn BAM I/O
- `archive_format.h` is NOT needed (no serialization in this class)

---

## Component 3: DataStats (thinned)

### What Remains

**Config:**
- `reference_` — `Reference*`
- `maximum_insert_length_` — `const uintSeqLen`
- `minimum_mapping_quality_` — `const uintQual`

**Owned sub-stats (unchanged):**
- `adapters_` — `AdapterStats`
- `coverage_` — `CoverageStats`
- `duplicates_` — `FragmentDuplicationStats`
- `errors_` — `ErrorStats`
- `fragment_distribution_` — `FragmentDistributionStats`
- `qualities_` — `QualityStats`
- `tiles_` — `TileStats`

**New owned component:**
- `read_sequence_stats_` — `ReadSequenceStats`

**Metadata scalars (unchanged):**
- `creation_time_`, `phred_quality_offset_`, `minimum_quality_`, `maximum_quality_`
- `minimum_read_length_on_reference_`, `maximum_read_length_on_reference_`
- `corrected_coverage_`, `total_number_reads_`

**Read-length histograms (unchanged):**
- `read_lengths_`, `read_lengths_by_fragment_length_`, `non_mapped_read_lengths_by_fragment_length_`

### Public API (unchanged)

All existing getters remain with identical signatures. Getters for ReadSequenceStats data forward to `read_sequence_stats_`:

```cpp
const Vect<uintFragCount>& ProperPairMappingQuality() const {
    return read_sequence_stats_.ProperPairMappingQuality();
}
// ... same pattern for all histogram getters
```

`ReadBam(...)` becomes:
```cpp
bool DataStats::ReadBam(...) {
    BamIngestionEngine engine;
    return engine.Run(bam_file, adapter_file, adapter_matrix,
                      variant_file, max_ref_seq_bin_size, num_threads,
                      calculate_bias, *this);
}
```

`Load`/`Save` unchanged — `serialize()` includes `read_sequence_stats_` as a member.

### Friend Declarations

```cpp
friend class BamIngestionEngine;  // needs access to sub-stats and metadata during Run()
// Existing friends remain: DataStatsTest, ProbabilityEstimatesTest, SimulatorTest
```

---

## File Layout

| File | Status | Content |
|------|--------|---------|
| `reseq/ReadSequenceStats.h` | New | Class declaration, inline getters |
| `reseq/ReadSequenceStats.cpp` | New | PrepareAccumulators, EvalBaseLevelStats, Finalize, Shrink |
| `reseq/BamIngestionEngine.h` | New | Class declaration, public Run + static utilities |
| `reseq/BamIngestionEngine.cpp` | New | All pipeline methods, threading, evaluation, validation |
| `reseq/DataStats.h` | Modified | Remove extracted members/methods, add read_sequence_stats_ member, add friend BamIngestionEngine |
| `reseq/DataStats.cpp` | Modified | Remove extracted method implementations, ReadBam becomes thin wrapper |
| `reseq/CMakeLists.txt` | Modified | Add ReadSequenceStats.cpp, BamIngestionEngine.cpp to reseq_lib |

---

## Extraction Order

Each step must build and pass all tests before proceeding:

1. **Extract ReadSequenceStats** — move histogram data + EvalBaseLevelStats + getters + serialize. DataStats forwards getters and serializes `read_sequence_stats_` as member.
2. **Extract BamIngestionEngine** — move pipeline methods + threading + predicates + counters. DataStats::ReadBam becomes thin wrapper.
3. **Clean up DataStats** — remove any dead includes, verify all forwarding getters, tidy friend declarations.
4. **Format + full test suite** — `make format`, `make build`, `make test`, `pre-commit run --all-files`.

---

## Verification Gate

- All existing unit tests pass unchanged (DataStatsTest, ProbabilityEstimatesTest, SimulatorTest)
- All regression tests pass unchanged (golden file comparisons)
- `make format-check` clean
- `pre-commit run --all-files` clean
- No public API changes — grep for `DataStats::` usage across codebase confirms no caller changes needed

---

## What This Does NOT Do

- Does not add new unit tests (deferred to Phase 7)
- Does not remove FRIEND_TEST or narrow friend access (deferred to Phase 6)
- Does not change any public API signatures
- Does not touch other god objects (Phases 5c-f)
- Does not refactor sub-stats classes (they are already single-responsibility)
