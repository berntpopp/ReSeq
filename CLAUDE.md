# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository

This is a fork (`berntpopp/ReSeq`) of upstream (`schmeing/ReSeq`). The upstream remote has been removed.

**All work stays in the fork.** Never add the upstream remote, create PRs on `schmeing/ReSeq`, or push to it. Use `--repo berntpopp/ReSeq` with all `gh` commands.

## Build and Test Commands

```bash
make build              # Configure (CMake) and build
make test               # Build and run all 25 unit tests
make format             # Format C++ (clang-format) and Python (ruff) in-place
make format-check       # Dry-run format check (used in CI)
make lint               # Run clang-tidy and ruff
make clean              # Remove build directory
make changelog          # Generate CHANGELOG.md via git-cliff
pre-commit run --all-files  # Run all pre-commit hooks
```

Tests are compiled into the main binary and run via `build/bin/reseq test`. GoogleTest filter syntax works: `build/bin/reseq test --gtest_filter="SimulatorTest.*"`.

The build requires: C++14 compiler, CMake 3.1+, Boost 1.48+ (serialization, program_options, filesystem, system, math), ZLIB, BZip2. Python bindings are OFF by default (`-DRESEQ_BUILD_PYTHON=ON` requires SWIG 3+ and python3-dev).

## Architecture

ReSeq is a bioinformatics tool that learns error/quality profiles from real Illumina paired-end sequencing data and uses them to simulate realistic reads.

### Commands (entry point: `reseq/main.cpp`)

| Command | Purpose |
|---------|---------|
| `illuminaPE` | Full pipeline: collect stats from BAM → estimate probabilities via IPF → simulate reads |
| `seqToIllumina` | Apply Illumina error/quality model to input sequences (no coverage model) |
| `queryProfile` | Extract info from `.reseq` stats files (fragment length bias, ref seq bias, etc.) |
| `replaceN` | Replace ambiguous bases (N) in reference sequences |
| `test` | Run GoogleTest unit test suite |

### Core Components (in `reseq/`)

Two CMake libraries:

**DataStatsInterface** (static) — statistics collection from BAM data:
- `DataStats` — top-level aggregator that orchestrates all sub-stats
- `AdapterStats`, `CoverageStats`, `ErrorStats`, `FragmentDistributionStats`, `FragmentDuplicationStats`, `QualityStats`, `TileStats` — each models a specific aspect of sequencing
- `Reference` — reference genome loading, surrounding context, excluded regions
- `Surrounding` / `SurroundingBase` — sequence context modeling for error patterns
- `Vect` — offset vector (indexed from non-zero starting position), used pervasively
- `utilities.hpp` — shared types, atomic vector wrapper, helper functions

**Main executable** — simulation and probability estimation:
- `ProbabilityEstimates` — Iterative Proportional Fitting (IPF) for multi-dimensional probability tables
- `Simulator` — block-based read simulation engine with threading support

### Vendored Dependencies (excluded from formatting/linting)

`seqan/` (bioinformatics), `googletest/` (testing), `nlopt/` (optimization), `skewer/` (adapter trimming, with local modifications), `2016-05-15_ROOTPWA/` (utilities)

### Test Structure

Each component has a `*Test.cpp` / `*Test.h` pair inheriting from `BasicTestClass.hpp` (which extends `::testing::Test`). Test data lives in `test/` (E. coli and Drosophila references, BAMs, adapters). Tests run as part of the main binary, not a separate test executable.

### Versioning

Single source of truth: `VERSION` file (currently `1.1.0`). CMake reads it at configure time. `CMakeConfig.h.in` generates version macros including `RESEQ_GIT_VERSION` from `git describe`.

## Code Style

- C++: clang-format (LLVM-based, 120 column limit, 4-space indent, C++14)
- Python: ruff (py37 target, 120 line length, rules: E/F/W/I/B/SIM)
- Commits: conventional commits (`feat:`, `fix:`, `build:`, `style:`, `test:`, `ci:`)
- Pre-commit hooks enforce formatting on `reseq/` and `python/` only

## CI

GitHub Actions (`.github/workflows/ci.yml`): builds with GCC 13 and Clang 17 on Ubuntu 24.04, runs format checks, and verifies version tags match `VERSION` file.
