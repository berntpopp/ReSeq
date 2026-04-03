# Phase 5a: main.cpp Decomposition — Design Specification

**Date:** 2026-04-03
**Status:** Draft (rev 2 — incorporates review findings)
**Phase:** 5a (God Object Decomposition — main.cpp)
**Prerequisite:** Phase 4 complete (PR #7, `2e65afb`)

---

## 1. Goal

Decompose `reseq/main.cpp` (1,167 lines) from a monolithic file containing 4 inline
command implementations + 8 helper functions into a slim dispatcher (~100 lines) with
command logic extracted into focused files under `reseq/cli/`.

This is a **strictly mechanical extraction** — no behavior changes, no API redesign, no
helper signature changes. Every function is moved verbatim. The dispatcher preserves
exact exit codes, stderr output, and help formatting.

## 2. Current Architecture

### File Structure

```
reseq/main.cpp (1,167 lines)
  Lines 1-64:     Includes, globals (kVerbosityLevel, kNoDebugOutput), SeqAn statics
  Lines 66-82:    AutoDetectThreads(num_threads, opt_desc, usage_str) → bool
  Lines 84-117:   DefaultExtensionFile(stats_in, extension) → string
  Lines 119-291:  GetDataStats(...) — complex, illuminaPE only
  Lines 293-300:  GetProbsOut(probs_out, fallback_out, opts_map) → void
  Lines 302-321:  PrepareProbabilityEstimation(probs_in, probs_out, standard_probs_out,
                                               loaded_stats, opts_map) → void
  Lines 323-335:  GetSeed(opts_map, return_code) → uintSeed
  Lines 337-388:  WriteSysError(...) — illuminaPE only
  Lines 390-405:  PrepareSimulation(...) — illuminaPE only
  Lines 408-453:  main() — general options, version, help, dispatch
  Lines 455-605:  queryProfile command (~150 lines)
  Lines 606-681:  replaceN command (~75 lines)
  Lines 682-1018: illuminaPE command (~337 lines, largest)
  Lines 1019-1153: seqToIllumina command (~135 lines)
  Lines 1154-1167: error handling, return
```

### Global Symbol Ownership

| Symbol | Declared In | Defined In | Notes |
|--------|-------------|------------|-------|
| `kVerbosityLevel` | `logging.hpp:24` | `main.cpp:20`, `test_main.cpp:7` | `atomic<uint16_t>`, read everywhere |
| `kNoDebugOutput` | `utilities.hpp:230` | `main.cpp:21`, `test_main.cpp:32` | `bool`, unused in main.cpp |
| SeqAn complement statics | N/A | `main.cpp:57-63` | Required for SeqAn initialization |

These globals are **defined** in the two entrypoint TUs (`main.cpp` and `test_main.cpp`)
but **declared** in headers consumed by `reseq_lib`. This means CLI code moved into
`reseq_lib` can reference them (via the `extern` declarations in headers), and the
linker resolves them from whichever entrypoint TU is linked. This is the existing
pattern — it works today and continues to work after extraction.

**Not addressed in this phase:** Moving these definitions into `reseq_lib` itself would
require a separate design (e.g., a `globals.cpp` in reseq_lib with weak/conditional
initialization). That is out of scope — the current two-definition-site pattern is
preserved exactly.

### Dispatch Behavior (exact current semantics)

| Scenario | Stderr Output | Exit Code |
|----------|---------------|-----------|
| No command (`reseq`) | general_usage string, NO opt_desc | 0 |
| `reseq --help` | general_usage + opt_desc_full | 0 |
| `reseq --version` | "ReSeq version X.Y" | 0 |
| Valid command | version banner + command output | 0 on success |
| Valid command with error | version banner + error | 1 |
| Unknown command | version banner + "Unrecognized command: '...'" + general_usage | 1 |

**Critical:** bare `reseq` with no command exits 0 (line 1166, `return_code` initialized
to 0 and never set in the no-command branch). The dispatcher must preserve this exactly.

### Helper Function Signatures (exact current)

```cpp
// Line 66 — returns bool (false = error, prints help internally)
bool AutoDetectThreads(uintNumThreads& num_threads,
                       const options_description& opt_desc,
                       const string& usage_str);

// Line 84 — no adapter_dir parameter
string DefaultExtensionFile(const string& stats_in, const string& extension);

// Line 293 — void, output via reference, takes fallback_out
void GetProbsOut(string& probs_out, const string& fallback_out,
                 const variables_map& opts_map);

// Line 302 — void, needs standard_probs_out AND loaded_stats
void PrepareProbabilityEstimation(string& probs_in, string& probs_out,
                                  const string& standard_probs_out,
                                  bool loaded_stats,
                                  const variables_map& opts_map);

// Line 323 — takes return_code by reference (sets to 1 on error)
uintSeed GetSeed(const variables_map& opts_map, int& return_code);
```

These signatures are **moved verbatim** — no simplification, no parameter changes.

### Helper Function Placement (corrected)

| Helper | Used By | Placement |
|--------|---------|-----------|
| `AutoDetectThreads` | replaceN, illuminaPE, seqToIllumina | cli_common (shared, 3 callers) |
| `GetSeed` | replaceN, WriteSysError, seqToIllumina | cli_common (shared, 3 callers) |
| `GetProbsOut` | PrepareProbabilityEstimation | cli_common (co-located with caller) |
| `PrepareProbabilityEstimation` | illuminaPE, seqToIllumina | cli_common (shared, 2 callers) |
| `DefaultExtensionFile` | GetDataStats only | illumina_pe.cpp (private, 1 caller) |
| `GetDataStats` | illuminaPE only | illumina_pe.cpp (private, 1 caller) |
| `WriteSysError` | illuminaPE only | illumina_pe.cpp (private, 1 caller) |
| `PrepareSimulation` | illuminaPE only | illumina_pe.cpp (private, 1 caller) |

**Change from rev 1:** `DefaultExtensionFile` moved from cli_common to illumina_pe.cpp
since it's only called by `GetDataStats` which is illuminaPE-private.

## 3. Target Architecture

### File Structure

```
reseq/main.cpp                (~100 lines) — globals, SeqAn statics, general opts, dispatch
reseq/cli/cli_common.h        (~40 lines)  — AutoDetectThreads, GetSeed, GetProbsOut,
                                              PrepareProbabilityEstimation declarations
reseq/cli/cli_common.cpp      (~100 lines) — implementations of above
reseq/cli/query_profile.h     (~15 lines)  — RunQueryProfile declaration
reseq/cli/query_profile.cpp   (~160 lines) — queryProfile implementation
reseq/cli/replace_n.h         (~15 lines)  — RunReplaceN declaration
reseq/cli/replace_n.cpp       (~85 lines)  — replaceN implementation
reseq/cli/illumina_pe.h       (~15 lines)  — RunIlluminaPE declaration
reseq/cli/illumina_pe.cpp     (~420 lines) — illuminaPE + DefaultExtensionFile, GetDataStats,
                                              WriteSysError, PrepareSimulation (file-static)
reseq/cli/seq_to_illumina.h   (~15 lines)  — RunSeqToIllumina declaration
reseq/cli/seq_to_illumina.cpp (~140 lines) — seqToIllumina implementation
```

### Command Interface

Each command is a free function in `reseq::cli` namespace. The interface passes
everything the command needs to reproduce exact current behavior:

```cpp
namespace reseq::cli {

// Each command receives:
//   args           — unrecognized_opts with command name already erased
//   num_threads    — from general options (may be 0 = auto-detect)
//   general_opts   — for checking help flag
//   opt_desc_full  — mutable ref, command adds its options for help display
//
// Returns: 0 on success, 1 on error (matching current return_code semantics)

int RunQueryProfile(const std::vector<std::string>& args,
                    uintNumThreads num_threads,
                    const boost::program_options::variables_map& general_opts,
                    boost::program_options::options_description& opt_desc_full);

int RunReplaceN(/* same signature */);
int RunIlluminaPE(/* same signature */);
int RunSeqToIllumina(/* same signature */);

} // namespace reseq::cli
```

### main.cpp Dispatcher (behavior-preserving)

The dispatcher must reproduce the exact current control flow. Key differences from
rev 1 that preserve behavior:

```cpp
int main(int argc, const char* argv[]) {
    // ... general options parsing (unchanged) ...

    int return_code = 0;  // MUST be 0 — bare `reseq` exits 0
    if (0 == unrecognized_opts.size()) {
        // No command: print general_usage only (NO opt_desc_full), exit 0
        cerr << general_usage << std::endl;
    } else {
        // Version banner (current exact format)
        printInfo << "Running ReSeq version " << RESEQ_VERSION_MAJOR << '.'
                  << RESEQ_VERSION_MINOR;

        if ("queryProfile" == unrecognized_opts.at(0)) {
            // ... erase, dispatch ...
        } else if ("replaceN" == unrecognized_opts.at(0)) {
            // ... erase, dispatch ...
        } else if ("illuminaPE" == unrecognized_opts.at(0)) {
            // ... erase, dispatch ...
        } else if ("seqToIllumina" == unrecognized_opts.at(0)) {
            // ... erase, dispatch ...
        } else {
            // Unknown command: exact current format
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
```

### CMake Changes

Add new source files to the `reseq` **executable** target, NOT `reseq_lib`. The CLI
command files are thin wrappers around `reseq_lib` — they are consumers, not library code.
This avoids expanding the `reseq_lib` surface and sidesteps the global symbol
ownership issue (the `reseq` executable already links `reseq_lib` + defines globals).

```cmake
# Production executable — main.cpp + CLI command files
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

**Change from rev 1:** CLI files go in the `reseq` executable target, not `reseq_lib`.
This keeps them out of the library, avoids the GTest::gtest PUBLIC linkage concern
(finding #6), and is correct — these files define CLI behavior, not reusable library code.

**Note on GTest PUBLIC linkage:** The existing `reseq_lib` links `GTest::gtest` as PUBLIC
(`reseq/CMakeLists.txt:30`) because production headers use `FRIEND_TEST` macros.
Fixing this is out of scope for Phase 5a (the comprehensive spec defers it to Phase 6e
after decomposition creates cleaner interfaces). We do not expand the problem by keeping
CLI code out of `reseq_lib`.

## 4. Migration Strategy

Strictly mechanical: move code, preserve all signatures and behavior.

1. **Add new regression tests for CLI behavior** (prerequisite — see Testing section).
   Commit.

2. **Create cli/ directory and cli_common** with shared helpers moved verbatim from
   main.cpp. main.cpp `#include`s cli_common.h and calls the helpers. Tests pass. Commit.

3. **Extract commands one at a time** (simplest first):
   - **replaceN** — smallest, uses AutoDetectThreads + GetSeed from cli_common
   - **queryProfile** — no shared helpers, self-contained
   - **seqToIllumina** — uses PrepareProbabilityEstimation from cli_common
   - **illuminaPE** — largest, carries its own private helpers (DefaultExtensionFile,
     GetDataStats, WriteSysError, PrepareSimulation)

   For each:
   - Create `cli/<command>.h` and `cli/<command>.cpp`
   - Move command logic **verbatim** into `Run<Command>()` function body
   - Add the mode print (e.g., `printInfo << "queryProfile mode" << std::endl;`) and
     command name erase into the Run function (these are currently per-command in main.cpp)
   - Replace inline code in main.cpp with function call
   - Update CMakeLists.txt
   - Build and test
   - Commit

4. **Clean up main.cpp** — remove dead includes, verify line count, format. Commit.

Each step is one commit. Tests pass at every step.

## 5. Invariants

| # | Invariant | How Verified |
|---|-----------|-------------|
| M1 | CLI output identical for all commands | Golden-file regression tests (stdout) + NEW stderr/exit-code tests |
| M2 | Exit codes preserved exactly | NEW: bare `reseq` → 0, unknown command → 1, `--help` → 0, `--version` → 0 |
| M3 | Help/version text unchanged | NEW: capture stderr for `--help` and `--version`, compare before/after |
| M4 | Unknown command error format preserved | NEW: test exact stderr output for unknown command |
| M5 | Global state initialization order | kVerbosityLevel set before any command runs (stays in main.cpp) |
| M6 | SeqAn static initialization | Complement functor statics remain in main.cpp translation unit |

## 6. Testing

### Existing Regression Test Limitations

The current regression harness (`RegressionTest.h:79-82`) suppresses stderr via
`2>/dev/null` and forces `--verbosity 0`. This means existing tests do NOT verify:
- General help text, subcommand help text, or banner formatting
- Unknown-command stderr output
- Bare-reseq exit code (no-command scenario)
- Version banner text

### New Regression Tests (prerequisite — added BEFORE extraction)

Add these tests to `reseq/RegressionTest.cpp` before any code moves:

| Test | What It Locks Down |
|------|-------------------|
| `BareReseqExitCode` | `reseq` with no args → exit code 0 |
| `UnknownCommandExitCode` | `reseq nonsense` → exit code 1 |
| `HelpExitCode` | `reseq --help` → exit code 0 |
| `VersionExitCode` | `reseq --version` → exit code 0 |
| `UnknownCommandStderr` | `reseq nonsense` → stderr contains "Unrecognized command: 'nonsense'" |
| `CommandHelpExitCode` | `reseq replaceN --help` → exit code 0 |

These tests use a new `RunReseqExitCode(args)` helper that does NOT suppress stderr
or add `--verbosity 0`, just captures the exit code. And a `RunReseqStderr(args)`
helper that captures stderr.

These tests establish the behavioral contract BEFORE extraction begins. If any
extraction step breaks CLI behavior, these tests catch it.

### Existing Golden-File Tests

The 10 existing regression tests remain the primary safety net for command output
correctness (stdout file content). They exercise all 4 commands via subprocess.

## 7. Verification Criteria

At every commit:
- `make build && make test` — all unit tests + regression tests pass
- `make format-check` — no formatting issues

After final commit:
- `wc -l reseq/main.cpp` — target ~100 lines
- All new CLI behavior tests pass
- All 10 golden-file regression tests pass
