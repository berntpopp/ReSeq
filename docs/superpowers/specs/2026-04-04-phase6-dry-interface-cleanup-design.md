# Phase 6: DRY & Interface Cleanup — Design Specification

**Date:** 2026-04-04
**Status:** Draft
**Scope:** Named constants, container type aliases, FRIEND_TEST removal, DataStatsInterface slimming
**Prerequisite:** Phase 5b (DataStats decomposition) merged. `make build && make test` passes on master.
**Parent spec:** `docs/superpowers/specs/2026-04-01-comprehensive-refactoring-design.md` (Phase 6)

---

## Scope Decisions

The original Phase 6 spec defines 5 sub-tasks (6a-6e). This implementation **defers 6a (StatVariable\<T\> registration)** because it touches serialization across all 8 Stats classes — high risk immediately after the Phase 5b serialization-preserving decomposition. The remaining 4 sub-tasks are lower risk and largely mechanical.

**In scope:** 6b, 6c, 6d, 6e
**Deferred:** 6a (can be revisited as a standalone phase)

---

## Execution Order

Bottom-up: foundations first, broadest change last.

| Step | Sub-task | What | Risk |
|------|----------|------|------|
| 1 | 6b | Named constants (`constants.hpp`) | LOW |
| 2 | 6c | Container type aliases (`container_types.hpp`) | LOW |
| 3 | 6e | FRIEND_TEST removal + gtest cleanup | LOW |
| 4 | 6d | Slim DataStatsInterface | MEDIUM |

Each step is merged to master before the next begins.

---

## Critical Invariants

1. **All existing tests pass** after each step.
2. **Binary serialization compatibility preserved** — no data layout changes, no field reordering.
3. **No public DataStats API changes** — DataStatsInterface changes are internal to its consumers.
4. **Conventional commits** with `(6)` scope tag and sub-task letter (e.g., `refactor(6b):`).

---

## Step 1: Named Constants (6b)

### Goal

Replace magic numbers with named domain constants. Improves readability and reduces error risk when the same value appears in array template parameters, loop bounds, and conditionals across 15+ files.

### Design

Create `reseq/constants.hpp`:

```cpp
#ifndef RESEQ_CONSTANTS_H
#define RESEQ_CONSTANTS_H

#include "utilities.hpp" // For type aliases (uintBaseCall, uintTempSeq, uintQual)

namespace reseq {

inline constexpr uintBaseCall kNumBases = 4;        // A, C, G, T
inline constexpr uintBaseCall kNumBasesN = 5;        // A, C, G, T, N
inline constexpr uintTempSeq kTemplateSegments = 2;  // first, second read
inline constexpr uintTempSeq kStrands = 2;           // forward, reverse
inline constexpr uintQual kPhredSangerOffset = 33;
inline constexpr uintQual kPhredIlluminaOffset = 64;
inline constexpr uint16_t kGCBins = 101;             // 0-100% inclusive

} // namespace reseq

#endif // RESEQ_CONSTANTS_H
```

### Replacement Rules

| Magic | Constant | Context | ~Count |
|-------|----------|---------|--------|
| `4` | `kNumBases` | Array template params, loop bounds for DNA bases | ~60 |
| `5` | `kNumBasesN` | Array template params, loop bounds for ACGTN | ~50 |
| `2` | `kTemplateSegments` | Array template params for first/second read | ~30 |
| `2` | `kStrands` | Array template params for forward/reverse | ~10 |
| `33` | `kPhredSangerOffset` | Quality offset comparison | ~3 |
| `64` | `kPhredIlluminaOffset` | Quality offset comparison | ~2 |
| `101` | `kGCBins` | GC content bin array sizes | ~12 |

**Not replaced:** Loop counters that count down (`for (auto base = 5; base--;)`) keep their numeric form — replacing with a constant would obscure the countdown pattern. Only array declarations and explicit comparisons are updated.

**Disambiguation:** Not every `4` or `5` is a base count. Each replacement must be verified in context. For example, `4` in `Surrounding::Length()` is unrelated to DNA bases.

### Verification

`make build && make test` — purely mechanical, no logic changes.

---

## Step 2: Container Type Aliases (6c)

### Goal

Replace deeply nested container declarations (up to 7 levels in ErrorStats/QualityStats) with readable type aliases. The aliases use constants from Step 1.

### Design

Create `reseq/container_types.hpp`:

```cpp
#ifndef RESEQ_CONTAINER_TYPES_H
#define RESEQ_CONTAINER_TYPES_H

#include <array>
#include <vector>

#include "constants.hpp"
#include "utilities.hpp"
#include "Vect.hpp"

namespace reseq {

// Atomic accumulator vector (thread-safe, filled during multi-threaded BAM pass)
template <typename T>
using AtomicVec = std::vector<utilities::VectorAtomic<T>>;

// Domain-indexed array aliases (use constants for sizes)
template <typename T>
using PerSegment = std::array<T, kTemplateSegments>;

template <typename T>
using PerStrand = std::array<T, kStrands>;

template <typename T>
using PerBase = std::array<T, kNumBases>;

template <typename T>
using PerBaseN = std::array<T, kNumBasesN>;

template <typename T>
using PerSegmentPerBase = PerSegment<PerBase<T>>;

template <typename T>
using PerSegmentPerBaseN = PerSegment<PerBaseN<T>>;

template <typename T>
using PerSegmentPerStrand = PerSegment<PerStrand<T>>;

template <typename T>
using PerSegmentPerStrandPerBase = PerSegment<PerStrand<PerBase<T>>>;

} // namespace reseq

#endif // RESEQ_CONTAINER_TYPES_H
```

### Application Scope

Apply to **header declarations only** — the 35-40 deeply nested type declarations in Stats class headers. Examples:

**Before (ErrorStats.h):**
```cpp
std::array<std::array<std::array<std::vector<std::vector<std::vector<
    utilities::VectorAtomic<uintNucCount>>>>, 5>, 4>, 2>
    tmp_called_bases_by_base_quality_per_tile_;
```

**After:**
```cpp
PerSegmentPerBase<PerBaseN<std::vector<std::vector<AtomicVec<uintNucCount>>>>>
    tmp_called_bases_by_base_quality_per_tile_;
```

**Not changed:**
- `.cpp` file local variables and loop structures — too much churn for marginal readability gain
- Containers where the array dimension doesn't correspond to a domain concept (e.g., arbitrary size arrays)
- Serialization code — field order must remain identical

### Verification

`make build && make test` — type aliases are transparent to the compiler.

---

## Step 3: FRIEND_TEST Removal & gtest Cleanup (6e)

### Goal

Remove all 6 `FRIEND_TEST` macros from production headers. Remove `gtest/gtest.h` include from `Vect.hpp`. Use Peer classes (Chromium/Abseil best practice) for cases requiring private access.

### Strategy Per Macro

| Location | Macro | Action |
|----------|-------|--------|
| `Vect.hpp` | `FRIEND_TEST(VectTest, BasicFunctionality)` | **Eliminate.** Test through public API (`.std()`, iterators, `size()`, `from()`, `to()`). |
| `Vect.hpp` | `FRIEND_TEST(VectTest, CopyAndClear)` | **Eliminate.** Same — public API is sufficient. |
| `DataStats.h` | `FRIEND_TEST(DataStatsTest, Construction)` | **Eliminate.** Test construction via public getters. The Construction test currently checks `read_lengths_.empty()` and `sequence_content_.empty()` — replace with `ReadLengths(0).size() == 0` and `SequenceContent(0, 0).size() == 0`. |
| `ReadSequenceStats.h` | `FRIEND_TEST(DataStatsTest, Construction)` | **Eliminate.** Same test, same fix as above. |
| `FragmentDistributionStats.h` | `FRIEND_TEST(FragmentDistributionStatsTest, UpdateRefSeqBias)` | **Peer class.** Add `friend class FragmentDistributionStatsPeer;` to header. Define `FragmentDistributionStatsPeer` in `FragmentDistributionStatsTest.cpp` with accessor methods. |
| `FragmentDuplicationStats.h` | `FRIEND_TEST(FragmentDuplicationStatsTest, DispersionCalculation)` | **Peer class.** Add `friend class FragmentDuplicationStatsPeer;` to header. Define in `FragmentDuplicationStatsTest.cpp`. |
| `FragmentDuplicationStats.h` | `FRIEND_TEST(FragmentDistributionStatsTest, BiasBinningAndFragmentCounts)` | **Peer class.** Covered by same `FragmentDuplicationStatsPeer` (accessed from FragmentDistributionStatsTest via include). |

### Peer Class Pattern

```cpp
// In FragmentDistributionStatsTest.cpp (NOT a production file)
class FragmentDistributionStatsPeer {
  public:
    static auto& GetRefSeqBias(FragmentDistributionStats& stats) {
        return stats.ref_seq_bias_;
    }
    // ... other accessors as needed by tests
};
```

Production header gets only: `friend class FragmentDistributionStatsPeer;`

### gtest Cleanup

After removing all `FRIEND_TEST` from `Vect.hpp`, remove the `#include "gtest/gtest.h"` line. This eliminates the transitive gtest dependency from all 8 Stats class headers.

### Verification

`make build && make test` — all tests must pass with the rewritten assertions. No test coverage loss.

---

## Step 4: Slim DataStatsInterface (6d)

### Goal

Reduce DataStatsInterface from 679 LOC / 166 methods to only the methods actually used by its two consumers, and remove the `.std()` wrapping layer.

### Design

#### 4a: Audit Usage

Grep `queryProfile` CLI command and `plotDataStats.py` for all DataStatsInterface method calls. Remove every method not called by either consumer.

#### 4b: Remove `.std()` Wrapping

Currently every getter wraps `Vect<T>` via `.std()` to return `pair<size_type, vector<T>>&`. This exists for Python/SWIG compatibility.

**Change:** Return `const Vect<T>&` directly from DataStatsInterface. Push the conversion to the Python side:

- Add a SWIG `%extend` or typemap that converts `Vect<T>` to a Python-friendly form, OR
- Add a small helper in `plotDataStats.py` that extracts offset + data from Vect objects

The choice depends on SWIG binding complexity — if typemaps are straightforward, use them; otherwise, handle in Python.

#### 4c: Simplify Forwarding

For remaining methods, keep the single-line forwarding pattern but with cleaner return types (no `.std()` call).

#### 4d: Update Consumers

- `queryProfile` CLI command: Update to use new return types (likely minimal changes — it already works with Vect through DataStats public API in other contexts).
- `plotDataStats.py`: Update to handle Vect objects or use the new SWIG typemap.

### Expected Outcome

- DataStatsInterface: ~60-100 methods (down from 166), ~200-300 LOC (down from 679)
- No `.std()` wrapping in C++
- Python compatibility maintained via SWIG or script-side conversion

### Verification

- `make build && make test`
- Run `plotDataStats.py` on a real `.reseq` profile to verify Python output is correct
- Run `queryProfile` on a real `.reseq` profile to verify CLI output is correct

---

## Files Created

| File | Step | Purpose |
|------|------|---------|
| `reseq/constants.hpp` | 6b | Named domain constants |
| `reseq/container_types.hpp` | 6c | Template aliases for nested containers |

## Files Modified (Summary)

| Step | Files | Nature of Change |
|------|-------|------------------|
| 6b | ~15 Stats/utility files | Replace magic numbers with constants |
| 6c | ~8 Stats headers | Replace nested type declarations with aliases |
| 6e | 4 production headers + 3 test files | Remove FRIEND_TEST, add Peer classes, remove gtest include |
| 6d | DataStatsInterface.h/.cpp, plotDataStats.py, queryProfile CLI | Remove dead methods, remove .std() wrapping |

## Risk Mitigations

| Risk | Mitigation |
|------|------------|
| Wrong magic number replaced | Each replacement verified in context; `4` in non-base contexts left as-is |
| Container alias breaks serialization | Aliases are transparent to compiler; serialization order unchanged |
| FRIEND_TEST removal loses test coverage | Rewrite tests to use public API or Peer; verify same assertions |
| DataStatsInterface slimming breaks Python | Test plotDataStats.py against real profile after changes |
| Type alias confuses IDE/tooling | Standard C++ using declarations; all major IDEs resolve them |
