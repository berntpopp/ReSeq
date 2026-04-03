# Phase 5a: main.cpp Decomposition — Design Specification

**Date:** 2026-04-03
**Status:** Draft (rev 3 — incorporates second review)
**Phase:** 5a (God Object Decomposition — main.cpp)
**Prerequisite:** Phase 4 complete (PR #7, `2e65afb`)

---

## 1. Goal

Decompose `reseq/main.cpp` (1,167 lines) into a slim dispatcher (~100 lines) with
command logic extracted into focused files under `reseq/cli/`.

This is a **strictly mechanical extraction** — every function is moved verbatim with
its exact current signature. No behavior changes, no API redesign, no parameter changes.

## 2. Current Architecture

### File Structure

```
reseq/main.cpp (1,167 lines)
  Lines 1-64:     Includes, globals, SeqAn statics
  Lines 66-82:    AutoDetectThreads(num_threads, opt_desc, usage_str) → bool
  Lines 84-117:   DefaultExtensionFile(file_name, extension) → bool (mutates file_name in-place)
  Lines 119-291:  GetDataStats(...) — illuminaPE only
  Lines 293-300:  GetProbsOut(probs_out, fallback_out, opts_map) → void
  Lines 302-321:  PrepareProbabilityEstimation(probs_in, probs_out, standard_probs_out,
                                               loaded_stats, opts_map) → void
  Lines 323-335:  GetSeed(opts_map) → uintSeed
  Lines 337-388:  WriteSysError(...) — illuminaPE only
  Lines 390-405:  PrepareSimulation(...) — illuminaPE only
  Lines 408-453:  main() — general options, version, help, dispatch
  Lines 455-605:  queryProfile command (~150 lines)
  Lines 606-681:  replaceN command (~75 lines)
  Lines 682-1018: illuminaPE command (~337 lines)
  Lines 1019-1153: seqToIllumina command (~135 lines)
  Lines 1154-1167: unknown command error, return
```

### Exact Helper Signatures (to be moved verbatim)

```cpp
// Line 66 — returns bool, prints help on failure internally
bool AutoDetectThreads(uintNumThreads& num_threads,
                       const options_description& opt_desc,
                       const string& usage_str);

// Line 84 — returns bool, mutates file_name in-place
bool DefaultExtensionFile(string& file_name, const string extension);

// Line 293 — void, output via probs_out reference
void GetProbsOut(string& probs_out, const string& fallback_out,
                 const variables_map& opts_map);

// Line 302 — void, needs standard_probs_out AND loaded_stats
void PrepareProbabilityEstimation(string& probs_in, string& probs_out,
                                  const string& standard_probs_out,
                                  bool loaded_stats,
                                  const variables_map& opts_map);

// Line 323 — takes only opts_map, returns uintSeed
uintSeed GetSeed(const variables_map& opts_map);
```

### Global Symbol Ownership

| Symbol | Declared In | Defined In |
|--------|-------------|------------|
| `kVerbosityLevel` | `logging.hpp:24` | `main.cpp:20`, `test_main.cpp:8` |
| `kNoDebugOutput` | `logging.hpp:25` | `main.cpp:22`, `test_main.cpp:9` |
| SeqAn complement statics | N/A | `main.cpp:62-63`, `test_main.cpp:34-35` |

These remain in their current TUs. Not changed in this phase.

### Exact Current Dispatch Behavior

Verified at runtime (`build/bin/reseq` on 2026-04-03):

| Scenario | Stderr Output | Exit Code |
|----------|---------------|-----------|
| `reseq` (no args) | general_usage only (no opt_desc) | **0** |
| `reseq --help` | same as bare reseq (general_usage only, no opt_desc) | **0** |
| `reseq --version` | "ReSeq version X.Y" | 0 |
| Valid command | "Running ReSeq version X.Y in <mode> mode" + command output | 0 on success |
| Valid command + `--help` | version banner + usage_str + combined opt_desc_full | 0 |
| Unknown command | version banner + "Unrecognized command: '<cmd>'" + general_usage | 1 |

**Critical details:**
- Bare `reseq` and `reseq --help` produce **identical output** and both exit 0.
  There is no separate `--help` branch at the top level — the no-command path at
  line 449-450 just prints `general_usage` and falls through to `return return_code`
  (which is initialized to 0).
- The version banner is a single `printInfo` line started in main (line 452-453)
  with " in <mode> mode" appended by the command branch (e.g., line 456-458).
  This is one continuous stderr line, not separate prints.

### Helper Function Placement

| Helper | Callers | Placement |
|--------|---------|-----------|
| `AutoDetectThreads` | replaceN, illuminaPE, seqToIllumina (3 callers) | cli_common |
| `GetSeed` | replaceN, WriteSysError, seqToIllumina (3 callers) | cli_common |
| `GetProbsOut` | PrepareProbabilityEstimation (1 caller, co-locate) | cli_common |
| `PrepareProbabilityEstimation` | illuminaPE, seqToIllumina (2 callers) | cli_common |
| `DefaultExtensionFile` | GetDataStats only (1 caller) | illumina_pe.cpp (file-static) |
| `GetDataStats` | illuminaPE only (1 caller) | illumina_pe.cpp (file-static) |
| `WriteSysError` | illuminaPE only (1 caller) | illumina_pe.cpp (file-static) |
| `PrepareSimulation` | illuminaPE only (1 caller) | illumina_pe.cpp (file-static) |

## 3. Target Architecture

### File Structure

```
reseq/main.cpp                (~100 lines) — globals, SeqAn statics, general opts, dispatch
reseq/cli/cli_common.h        (~40 lines)  — AutoDetectThreads, GetSeed, GetProbsOut,
                                              PrepareProbabilityEstimation declarations
reseq/cli/cli_common.cpp      (~100 lines) — implementations (verbatim from main.cpp)
reseq/cli/query_profile.h     (~15 lines)  — RunQueryProfile declaration
reseq/cli/query_profile.cpp   (~160 lines) — queryProfile implementation
reseq/cli/replace_n.h         (~15 lines)  — RunReplaceN declaration
reseq/cli/replace_n.cpp       (~85 lines)  — replaceN implementation
reseq/cli/illumina_pe.h       (~15 lines)  — RunIlluminaPE declaration
reseq/cli/illumina_pe.cpp     (~420 lines) — illuminaPE + 4 file-static helpers
reseq/cli/seq_to_illumina.h   (~15 lines)  — RunSeqToIllumina declaration
reseq/cli/seq_to_illumina.cpp (~140 lines) — seqToIllumina implementation
```

### Command Interface

Each command receives the full context needed to reproduce current behavior:

```cpp
namespace reseq::cli {

// args:          unrecognized_opts with command name already erased (main does the erase)
// num_threads:   from general options (may be 0 = auto-detect)
// general_opts:  for checking help flag
// opt_desc_full: mutable ref — command adds its options for combined help display
//
// Returns: 0 on success, 1 on error

int RunQueryProfile(const std::vector<std::string>& args,
                    uintNumThreads num_threads,
                    const boost::program_options::variables_map& general_opts,
                    boost::program_options::options_description& opt_desc_full);

// Same signature for RunReplaceN, RunIlluminaPE, RunSeqToIllumina
}
```

### Ownership of Command-Name Stripping and Mode Banner

**main.cpp does both** (matching current code exactly):

1. main.cpp starts the version banner: `printInfo << "Running ReSeq version X.Y";`
2. main.cpp reads `unrecognized_opts.at(0)` to determine command
3. Each command's Run function appends " in <mode> mode\n" to the banner and
   completes the line (matching the current continuous-line pattern)
4. main.cpp does NOT erase the command name — each Run function receives `args`
   with command name **already erased by main.cpp before the call**

Wait — re-reading the current code: each command branch does its OWN erase
(e.g., line 459, 610, 686, 1023). So the erase happens inside the command block.
For consistency, **main.cpp erases before calling Run**, and the Run function
does NOT erase. This is cleaner and matches the interface doc above.

The mode banner print ("in queryProfile mode\n") moves into Run* because it's
command-specific. But the leading "Running ReSeq version X.Y" stays in main.cpp
(it's the same for all commands). The Run function's first action is:

```cpp
if (2 < kVerbosityLevel) {
    std::cerr << " in queryProfile mode" << std::endl;
}
```

This continues the line started by main.cpp's `printInfo << "Running ReSeq version..."`.

### CMake Changes

CLI files go in the `reseq` executable target (not `reseq_lib`):

```cmake
add_executable(reseq
  main.cpp
  cli/cli_common.cpp
  cli/query_profile.cpp
  cli/replace_n.cpp
  cli/illumina_pe.cpp
  cli/seq_to_illumina.cpp
)
target_link_libraries(reseq PRIVATE reseq_lib)
```

## 4. Migration Strategy

### Step 0: Add CLI behavior regression tests (prerequisite)

Before any code moves, add tests that lock down exact CLI behavior. See Testing section.

### Step 1: Extract shared helpers into cli_common

Move `AutoDetectThreads`, `GetSeed`, `GetProbsOut`, `PrepareProbabilityEstimation`
verbatim from main.cpp to `cli/cli_common.cpp`. main.cpp includes `cli/cli_common.h`
and calls the functions. Tests pass. Commit.

### Step 2-5: Extract commands one at a time

Order: replaceN → queryProfile → seqToIllumina → illuminaPE (simplest first).

For each command:
1. Create `cli/<command>.h` with Run declaration
2. Create `cli/<command>.cpp` — move command logic verbatim into Run function body
3. For illuminaPE: also move `DefaultExtensionFile`, `GetDataStats`, `WriteSysError`,
   `PrepareSimulation` as file-static (anonymous namespace) helpers
4. In main.cpp: erase command name, call Run function
5. Update CMakeLists.txt
6. Build and test. Commit.

### Step 6: Clean up main.cpp

Remove dead includes, verify ~100 lines, format. Commit.

## 5. Invariants

| # | Invariant | How Verified |
|---|-----------|-------------|
| M1 | Bare `reseq` prints general_usage and exits 0 | NEW: `BareReseqExitCode` test |
| M2 | `reseq --help` produces same output as bare reseq, exits 0 | NEW: `HelpSameAsBare` test |
| M3 | `reseq --version` prints version, exits 0 | Existing: `VersionOutput` test |
| M4 | Unknown command prints "Unrecognized command" + usage, exits 1 | NEW: `UnknownCommandBehavior` test |
| M5 | All command outputs unchanged | Existing: golden-file regression tests (replaceN, queryProfile) |
| M6 | Global state initialization order | kVerbosityLevel set before any command (stays in main.cpp) |
| M7 | SeqAn static initialization | Complement statics remain in main.cpp TU |

## 6. Testing

### Actual Current Regression Coverage (corrected)

| Command | Covered By | Coverage Level |
|---------|-----------|----------------|
| `replaceN` | `ReplaceN` test | Full (stdout file diff) |
| `queryProfile` | 3 tests on generated profile + 3 on Zenodo profile | Full (stdout/file diff) |
| `illuminaPE` | `GenerateEcoliProfile()` (--statsOnly --noBias only) | **Partial** — stats-only path, skips IPF+simulation |
| `seqToIllumina` | **None** | **No direct regression test** |

**Implication:** illuminaPE and seqToIllumina extractions carry higher risk. The
extraction must be strictly verbatim (copy-paste, no edits). Manual smoke testing
of the full illuminaPE pipeline and seqToIllumina is recommended after extraction.

### New CLI Behavior Tests (prerequisite — BEFORE extraction)

Add a `RunReseqExitOnly(args)` helper to `RegressionTest.h` that runs the binary
WITHOUT `--verbosity 0` or `2>/dev/null`, returning only the exit code. Add a
`RunReseqCaptureStderr(args)` helper (already exists) for stderr checks.

New tests in `RegressionTest.cpp`:

| Test | What It Locks Down | Assertion |
|------|-------------------|-----------|
| `BareReseqExitCode` | `reseq` (no args) → exit 0 | `EXPECT_EQ(0, rc)` |
| `HelpSameAsBare` | `reseq --help` output == bare `reseq` output | `EXPECT_EQ(bare_stderr, help_stderr)` |
| `UnknownCommandExitCode` | `reseq badcmd` → exit 1 | `EXPECT_EQ(1, rc)` |
| `UnknownCommandStderr` | stderr contains "Unrecognized command: 'badcmd'" | `EXPECT_NE(npos, stderr.find(...))` |
| `CommandHelpExitCode` | `reseq replaceN --help` → exit 0 | `EXPECT_EQ(0, rc)` |

These are **exit code + substring checks**, not full stderr snapshots. Full stderr
comparison would be fragile (version numbers, path differences). The goal is to
catch behavioral regressions (wrong exit code, missing error message), not lock
down exact formatting.

### Existing Tests Preserved

All 10 existing regression tests remain unchanged and serve as the primary safety
net for command output correctness.

## 7. Verification Criteria

At every commit:
- `make build && make test` — all tests pass
- `make format-check` — clean

After final commit:
- `wc -l reseq/main.cpp` — target ~100 lines
- All new CLI behavior tests pass
- All existing regression tests pass
- Manual smoke test: `reseq illuminaPE --help` and `reseq seqToIllumina --help`
  produce expected help output
