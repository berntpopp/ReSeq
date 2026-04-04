# Phase 7: Test Coverage & CI Instrumentation — Design Spec

**Goal:** Establish coverage visibility, fix the CI coverage job, fill critical unit test gaps, and verify test isolation. No hard coverage threshold — use auto-ratchet (Codecov `target: auto`) to prevent regression.

**Prerequisite:** Phase 6 merged. `make build && make test` passes on master.

---

## Current State

- **141 tests** (116 unit + 25 regression) across 16 test files
- **3 core classes with 0 unit tests:** ErrorStats, QualityStats, FragmentDuplicationStats
- **2 Phase 5 classes with 0 unit tests:** BamIngestionEngine, ReadSequenceStats
- **CI coverage job broken:** `geninfo: ERROR: Unexpected negative count` from non-atomic gcov counters in threaded code
- **No Codecov integration:** coverage report uploaded as artifact only, no PR annotations or badges
- **Tests run serially** (`ctest -j1`) — no verification that tests are order-independent

---

## Sub-tasks

### 7e: Fix Coverage CI + Codecov Integration

**Priority:** First — enables visibility for all subsequent work.

#### 7e.1: Fix gcov negative count error

Add `-fprofile-update=atomic` to coverage compile options in `reseq/CMakeLists.txt`:

```cmake
if(CODE_COVERAGE)
    target_compile_options(reseq_lib PRIVATE --coverage -fprofile-update=atomic)
    target_link_options(reseq_lib PRIVATE --coverage)
    target_compile_options(reseq_test PRIVATE --coverage -fprofile-update=atomic)
    target_link_options(reseq_test PRIVATE --coverage)
endif()
```

This makes gcov counter increments atomic, eliminating the race condition that produces negative counts in multi-threaded code.

#### 7e.2: Fix lcov version mismatch

Add `--gcov-tool gcov-13` to lcov commands in `.github/workflows/ci.yml` coverage job to ensure gcov version matches the GCC 13 compiler used for the coverage build.

#### 7e.3: Integrate Codecov

Add `codecov/codecov-action@v5` step after lcov report generation in `.github/workflows/ci.yml`:

```yaml
- name: Upload coverage to Codecov
  uses: codecov/codecov-action@v5
  with:
    files: build/coverage.info
    fail_ci_if_error: true
    token: ${{ secrets.CODECOV_TOKEN }}
```

Requires adding `CODECOV_TOKEN` as a repository secret (avoids tokenless upload issues on protected branches).

#### 7e.4: Configure Codecov auto-ratchet

Create `codecov.yml` at repository root:

```yaml
coverage:
  status:
    project:
      default:
        target: auto      # must not decrease from base branch
        threshold: 1%     # tolerance for measurement flakiness
    patch:
      default:
        target: 80%       # new/changed code must be 80% covered
```

This enforces:
- **Project-level:** coverage cannot decrease (ratchet)
- **Patch-level:** new code in PRs must be at least 80% covered

#### 7e.5: Add coverage badge to README

Add Codecov badge after the existing project description in `README.md`.

---

### 7f: Test Isolation Verification

**Priority:** Quick CI change, run in parallel with 7e.

Add `--gtest_shuffle --gtest_random_seed=0` to the test execution command in CI's `build-and-test` job. Seed `0` means "use current time" — each CI run uses a different test order.

Not using `ctest --schedule-random` because all GoogleTest cases run as a single CTest test case. The shuffle flag randomizes order within the GoogleTest binary.

**If shuffle reveals failures:** Document the dependency and fix it. The existing `Register()` pattern in test classes may cause issues — if so, refactor to use GoogleTest's `SetUpTestSuite()`.

---

### 7a: Fill Unit Test Gaps (0-test classes)

**Priority:** High-impact — these are the largest coverage gaps.

#### 7a.1: ErrorStats tests

Create tests in `reseq/ErrorStatsTest.cpp` (file exists but has 0 TEST/TEST_F):

| Test | What it verifies |
|------|------------------|
| `Prepare` | Allocates tmp vectors to correct dimensions for given tile/quality/position sizes |
| `AddBaseAndFinalize` | Accumulate bases via `AddBase()`, finalize, verify histograms are non-empty and consistent |
| `AddInDelAndFinalize` | Accumulate indels via `AddInDel()`, finalize, verify indel histograms |
| `Shrink` | After finalize, shrink removes trailing zeros without losing data |
| `PreparePlotting` | Derived plotting variables (called_bases_by_base_quality_, etc.) computed correctly |

Use synthetic setup: create ErrorStats, call `Prepare()` with small dimensions, inject known data via `AddBase()`/`AddInDel()`, then verify output.

#### 7a.2: QualityStats tests

Create tests in `reseq/QualityStatsTest.cpp` (file exists but has 0 TEST/TEST_F):

| Test | What it verifies |
|------|------------------|
| `Prepare` | Allocates accumulators to correct dimensions |
| `AddQualityAndFinalize` | Accumulate quality data, finalize, verify per-tile and per-strand histograms |
| `Shrink` | Shrink preserves data integrity |
| `SummaryStatistics` | Mean, median, quartiles, min, max computed correctly from known input |

#### 7a.3: FragmentDuplicationStats tests

Create tests in `reseq/FragmentDuplicationStatsTest.cpp` (file exists but has 0 TEST/TEST_F):

| Test | What it verifies |
|------|------------------|
| `AddDuplication` | Counts below kMaxDuplication go to correct bin, above goes to overflow bin |
| `FinalizeDuplicationVector` | Tmp vector correctly finalized into duplication_number_ Vect |
| `DuplicationNumber` | Public getter returns correct data after finalization |

These are simpler tests — FragmentDuplicationStats has a small public API.

---

### 7b: Tests for Phase 5 Decomposed Classes

#### 7b.1: BamIngestionEngine tests

Create `reseq/BamIngestionEngineTest.cpp` + `reseq/BamIngestionEngineTest.h` (new files):

| Test | What it verifies |
|------|------------------|
| `ProcessEcoliBam` | Process ecoli-SRR490124-4pairs.bam, verify record counts match expected |
| `MappingQualityFilter` | Records below minimum mapping quality are excluded |
| `ProperPairDetection` | Proper vs improper pairs classified correctly |

Uses existing ecoli BAM test data. Test fixture follows `BasicTestClassWithReference` pattern.

#### 7b.2: ReadSequenceStats tests

Create `reseq/ReadSequenceStatsTest.cpp` + `reseq/ReadSequenceStatsTest.h` (new files):

| Test | What it verifies |
|------|------------------|
| `PrepareAndFinalize` | Accumulators allocated, finalized into histograms |
| `MappingQualityAccumulation` | `IncrementMappingQuality()` correctly populates proper/improper/single histograms |
| `Shrink` | Shrink preserves data integrity |

---

### 7c: E2E Edge-Case Tests

Add to `reseq/RegressionTest.cpp`:

| Test | Input | Expected |
|------|-------|----------|
| `InvalidBamPath` | Non-existent BAM file path | Non-zero exit, error message on stderr |
| `MissingReference` | Valid BAM, invalid reference path | Non-zero exit, error message |
| `EmptyBam` | Valid BAM header, zero records | Graceful handling (zero exit or documented error) |
| `CorruptReseqFile` | Truncated/garbage `.reseq` file | Load returns false, no crash |

These use subprocess execution (same pattern as existing regression tests) to verify the CLI handles error paths cleanly.

---

### 7d: Multi-Thread Invariant Tests — DEFERRED

The original Phase 7 spec included multi-thread invariant tests (byte-identity across thread counts, statistical equivalence). This is deferred because:

1. **TSan already runs in CI** and catches data races
2. **Clang thread safety annotations** (compile-time) would provide higher ROI — consider for a future phase
3. The statistical equivalence tests require careful threshold tuning and are prone to flakiness

If thread-related bugs surface, revisit this sub-task.

---

## Verification Gate

After all sub-tasks complete:

- [ ] CI coverage job passes (no negative count errors)
- [ ] Codecov integration active: badge in README, PR annotations working
- [ ] `codecov.yml` enforces auto-ratchet + 80% patch coverage
- [ ] `--gtest_shuffle` runs in CI without failures
- [ ] ErrorStats, QualityStats, FragmentDuplicationStats each have >= 3 unit tests
- [ ] BamIngestionEngine and ReadSequenceStats each have >= 2 unit tests
- [ ] E2E error-path tests added (>= 3 new regression tests)
- [ ] `make build && make test` passes

---

## Files Created/Modified

| File | Action |
|------|--------|
| `reseq/CMakeLists.txt` | Add `-fprofile-update=atomic` to coverage options |
| `.github/workflows/ci.yml` | Fix lcov, add Codecov upload, add gtest_shuffle |
| `codecov.yml` | New — auto-ratchet configuration |
| `README.md` | Add Codecov badge |
| `reseq/ErrorStatsTest.cpp` | Add unit tests |
| `reseq/QualityStatsTest.cpp` | Add unit tests |
| `reseq/FragmentDuplicationStatsTest.cpp` | Add unit tests |
| `reseq/BamIngestionEngineTest.cpp` | New test file |
| `reseq/BamIngestionEngineTest.h` | New test header |
| `reseq/ReadSequenceStatsTest.cpp` | New test file |
| `reseq/ReadSequenceStatsTest.h` | New test header |
| `reseq/RegressionTest.cpp` | Add edge-case E2E tests |

---

## What This Phase Does NOT Do

- No hard coverage threshold (aspirational 70-75%, enforced only via auto-ratchet)
- No multi-thread invariant tests (deferred — TSan covers this)
- No CLI module unit tests (covered by existing 25 regression tests)
- No Clang thread safety annotations (future phase candidate)
- No test parallelization (tests remain serial due to existing `Register()` dependencies)
