# Modernize ReSeq Project Tooling — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add formatting, linting, versioning, CI, and developer convenience tooling to the ReSeq C++14 project while preserving the existing supported-platform floor.

**Architecture:** Config-file-first approach — add all tool configs and a Makefile first, then normalize the codebase with a one-time format commit, then enable enforcement in hooks and CI. Python bindings become an explicit CMake option (OFF by default). CMake minimum stays at 3.1.

**Tech Stack:** clang-format, clang-tidy, ruff, git-cliff, GNU Make, GitHub Actions

---

## Compatibility Policy

These floors are preserved (matching README.md):

| Dependency | Floor | Notes |
|------------|-------|-------|
| CMake | 3.1 (keep `CMakeLists.txt:1` as-is) | No `project(VERSION ${var})` — parse VERSION manually |
| Python | 3.6 | Scripts have no f-strings, no 3.7+ syntax |
| SWIG | 3.0 | Optional; Python bindings off by default |
| GCC | 7.2+ (C++14) | CI tests 13; older supported but not CI-tested |
| Clang | any C++14 | CI tests 17 |

## Scope

**Vendored dirs excluded from ALL formatting/linting:**
`seqan/`, `googletest/`, `nlopt/`, `skewer/`, `2016-05-15_ROOTPWA/`

**Project source files (formatting + linting targets):**
`reseq/*.cpp`, `reseq/*.h`, `reseq/*.hpp`, `python/*.py`

**Python files in scope:** `python/reseq-prepare-names.py`, `python/plotDataStats.py`
(Not `python/DataStats.i` — that's a SWIG interface, not Python.)

---

## Task 1: EditorConfig

**Files:**
- Create: `.editorconfig`

- [ ] **Step 1: Create `.editorconfig`**

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{cpp,h,hpp}]
indent_style = space
indent_size = 4

[*.py]
indent_style = space
indent_size = 4

[*.{yml,yaml}]
indent_style = space
indent_size = 2

[CMakeLists.txt]
indent_style = space
indent_size = 2

[*.cmake]
indent_style = space
indent_size = 2

[Makefile]
indent_style = tab
```

- [ ] **Step 2: Commit**

```bash
git add .editorconfig
git commit -m "build: add .editorconfig for consistent editor settings"
```

---

## Task 2: clang-format Config (no enforcement yet)

Vendored dirs do NOT need `.clang-format` disable files because all formatting
commands, hooks, and CI target only `reseq/` and `python/` by explicit file
enumeration. (`googletest/` already has its own upstream `.clang-format` — do
not modify it.)

**Files:**
- Create: `.clang-format`

- [ ] **Step 1: Create root `.clang-format`**

```yaml
---
Language: Cpp
BasedOnStyle: LLVM
Standard: c++14
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 120
BreakBeforeBraces: Attach
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
SortIncludes: CaseInsensitive
IncludeBlocks: Preserve
PointerAlignment: Left
SpaceAfterCStyleCast: false
```

- [ ] **Step 2: Commit**

```bash
git add .clang-format
git commit -m "build: add clang-format config (not yet enforced)"
```

---

## Task 3: Python Tooling Config

**Files:**
- Create: `pyproject.toml`

- [ ] **Step 1: Create `pyproject.toml`**

```toml
[tool.ruff]
target-version = "py36"
line-length = 120

[tool.ruff.lint]
select = ["E", "F", "W", "I", "B", "SIM"]
# E/F/W = pycodestyle + pyflakes
# I = isort
# B = flake8-bugbear
# SIM = flake8-simplify
# NOTE: UP (pyupgrade) omitted — would push syntax beyond py36 floor

[tool.ruff.lint.per-file-ignores]
# plotDataStats.py has long lines in matplotlib calls
"python/plotDataStats.py" = ["E501"]
```

Note: `target-version = "py36"` matches the README-documented Python 3.6.11 floor.
No `UP` (pyupgrade) rules — those push syntax that requires Python 3.9+.

- [ ] **Step 2: Verify ruff runs cleanly (informational only)**

```bash
ruff check python/ || echo "Issues found — will be fixed in format commit"
```

- [ ] **Step 3: Commit**

```bash
git add pyproject.toml
git commit -m "build: add ruff config for Python linting (py36 target)"
```

---

## Task 4: Versioning (VERSION file + CMake, no minimum bump)

**Files:**
- Create: `VERSION`
- Modify: `CMakeLists.txt:1-2, 62-64, 66-70`
- Modify: `CMakeConfig.h.in`
- Modify: `reseq/main.cpp:458, 464, 479`

- [ ] **Step 1: Create `VERSION` file**

```
1.1.0
```

- [ ] **Step 2: Modify `CMakeLists.txt` — replace version handling**

Replace lines 1-2:
```cmake
cmake_minimum_required(VERSION 3.1) 
project(reseq)
```
With:
```cmake
cmake_minimum_required(VERSION 3.1)

# Read version from VERSION file (single source of truth)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RESEQ_VERSION_RAW)
string(STRIP "${RESEQ_VERSION_RAW}" RESEQ_VERSION)
string(REPLACE "." ";" RESEQ_VERSION_LIST ${RESEQ_VERSION})
list(GET RESEQ_VERSION_LIST 0 RESEQ_VERSION_MAJOR)
list(GET RESEQ_VERSION_LIST 1 RESEQ_VERSION_MINOR)
list(GET RESEQ_VERSION_LIST 2 RESEQ_VERSION_PATCH)

project(reseq LANGUAGES CXX)
```

Replace lines 62-64:
```cmake
# Version
set(RESEQ_VERSION_MAJOR 1)
set(RESEQ_VERSION_MINOR 1)
```
With:
```cmake
# Version (read from VERSION file above)
# Optional: git describe for dev builds
find_package(Git QUIET)
set(RESEQ_GIT_DESCRIBE "v${RESEQ_VERSION}")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE RESEQ_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_RESULT
  )
  if(NOT GIT_RESULT EQUAL 0)
    set(RESEQ_GIT_DESCRIBE "v${RESEQ_VERSION}")
  endif()
endif()
```

Also add after the `set(CMAKE_CXX_STANDARD_REQUIRED ON)` line:
```cmake
# Export compile commands for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

- [ ] **Step 3: Update `CMakeConfig.h.in`**

Replace entire file:
```c
#ifndef CMAKECONFIG_H
#define CMAKECONFIG_H

#define RESEQ_VERSION_MAJOR @RESEQ_VERSION_MAJOR@
#define RESEQ_VERSION_MINOR @RESEQ_VERSION_MINOR@
#define RESEQ_VERSION_PATCH @RESEQ_VERSION_PATCH@
#define RESEQ_VERSION "@RESEQ_VERSION@"
#define RESEQ_GIT_VERSION "@RESEQ_GIT_DESCRIBE@"
#define PROJECT_SOURCE_DIR "@PROJECT_SOURCE_DIR@"

#endif // CMAKECONFIG_H
```

- [ ] **Step 4: Update `reseq/main.cpp` version output**

Replace line 458:
```cpp
		cerr << "ReSeq version " << RESEQ_VERSION_MAJOR << '.' << RESEQ_VERSION_MINOR << std::endl;
```
With:
```cpp
		cerr << "ReSeq version " << RESEQ_VERSION << " (" << RESEQ_GIT_VERSION << ")" << std::endl;
```

Replace line 464:
```cpp
		"Version: "+to_string(RESEQ_VERSION_MAJOR)+'.'+to_string(RESEQ_VERSION_MINOR)+'\n'+
```
With:
```cpp
		"Version: " RESEQ_VERSION "\n"+
```

Replace line 479:
```cpp
		printInfo << "Running ReSeq version " << RESEQ_VERSION_MAJOR << '.' << RESEQ_VERSION_MINOR;
```
With:
```cpp
		printInfo << "Running ReSeq version " << RESEQ_VERSION;
```

- [ ] **Step 5: Build and test**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
build/bin/reseq --version
# Expected: "ReSeq version 1.1.0 (v1.1.0-<hash>)"
build/bin/reseq test
# Expected: all tests pass
```

- [ ] **Step 6: Commit**

```bash
git add VERSION CMakeLists.txt CMakeConfig.h.in reseq/main.cpp
git commit -m "build: single-source version from VERSION file with git describe"
```

---

## Task 5: Python Bindings CMake Option

**Files:**
- Modify: `CMakeLists.txt:86`
- Modify: `python/CMakeLists.txt:1-8`

- [ ] **Step 1: Add option to `CMakeLists.txt`**

Replace line 86:
```cmake
# add_subdirectory(python) # Requires SWIG; enable when SWIG 3+ and python3-dev are installed
```
With:
```cmake
option(RESEQ_BUILD_PYTHON "Build Python bindings (requires SWIG 3+ and python3-dev)" OFF)
if(RESEQ_BUILD_PYTHON)
  add_subdirectory(python)
endif()
```

- [ ] **Step 2: Rewrite `python/CMakeLists.txt` to fail clearly when deps missing**

Replace lines 1-10 of `python/CMakeLists.txt`:
```cmake
find_program(PYTHON "python")

if (PYTHON)
	SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
	SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

	# Python packages
	FIND_PACKAGE(SWIG 3 REQUIRED)
	include(${SWIG_USE_FILE})
	FIND_PACKAGE(PythonLibs 3 REQUIRED)
```
With:
```cmake
# This subdirectory is only added when RESEQ_BUILD_PYTHON=ON.
# Fail clearly if prerequisites are missing rather than silently no-op.
find_program(PYTHON "python3" "python")
if(NOT PYTHON)
	message(FATAL_ERROR "RESEQ_BUILD_PYTHON=ON but no python3 interpreter found")
endif()

SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Python packages
FIND_PACKAGE(SWIG 3 REQUIRED)
include(${SWIG_USE_FILE})
FIND_PACKAGE(PythonLibs 3 REQUIRED)
```

Also remove the corresponding closing `endif()` at the end of the file (line 58)
since the `if(PYTHON)` guard is replaced by the `FATAL_ERROR`.

- [ ] **Step 3: Verify build works with default (OFF)**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
build/bin/reseq test
```

- [ ] **Step 4: Verify ON fails clearly without SWIG**

```bash
cmake -S . -B build-py -DRESEQ_BUILD_PYTHON=ON 2>&1 | grep -i "error\|fatal"
# Expected: clear FATAL_ERROR about missing SWIG or python
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt python/CMakeLists.txt
git commit -m "build: make Python bindings opt-in via RESEQ_BUILD_PYTHON

OFF by default. When ON, fails clearly if SWIG or Python are missing
instead of silently skipping."
```

---

## Task 6: Makefile

**Files:**
- Create: `Makefile`

- [ ] **Step 1: Create `Makefile`**

```makefile
BUILD_DIR ?= build
BUILD_TYPE ?= RelWithDebInfo
CMAKE_FLAGS ?=

# File lists via git ls-files — safe with spaces, only tracked files
CXX_SOURCES = $(shell git ls-files 'reseq/*.cpp' 'reseq/*.h' 'reseq/*.hpp')
PY_SOURCES  = $(shell git ls-files 'python/*.py')

.PHONY: all configure build test format format-check lint clean install help

all: build

configure:
	cmake -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
		$(CMAKE_FLAGS)

build: configure
	cmake --build $(BUILD_DIR) -j$$(nproc)

test: build
	$(BUILD_DIR)/bin/reseq test

format:
	clang-format -i $(CXX_SOURCES)
	ruff format $(PY_SOURCES) || echo "ruff not installed, skipping Python format"

format-check:
	clang-format --dry-run --Werror $(CXX_SOURCES)
	ruff format --check $(PY_SOURCES)

lint:
	clang-tidy -p $(BUILD_DIR)/ $(filter %.cpp,$(CXX_SOURCES))
	ruff check $(PY_SOURCES)

clean:
	rm -rf $(BUILD_DIR)

install: build
	cmake --install $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  build        Configure and build (default)"
	@echo "  test         Build and run unit tests"
	@echo "  format       Format C++ and Python files in-place"
	@echo "  format-check Dry-run format check (CI use)"
	@echo "  lint         Run clang-tidy and ruff"
	@echo "  clean        Remove build directory"
	@echo "  install      Build and install"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_DIR    Build directory (default: build)"
	@echo "  BUILD_TYPE   CMake build type (default: RelWithDebInfo)"
	@echo "  CMAKE_FLAGS  Extra flags passed to cmake"
```

- [ ] **Step 2: Verify `make build` and `make test`**

```bash
make clean
make test
# Expected: configures, builds, runs all unit tests, all pass
```

- [ ] **Step 3: Commit**

```bash
git add Makefile
git commit -m "build: add Makefile with build/test/format/lint targets"
```

---

## Task 7: One-Time Format Normalization

This MUST happen before any format enforcement (hooks/CI). It is a single commit
that touches only formatting — no logic changes — so it can be reviewed as such.

**Files:**
- Modify: all `reseq/*.cpp`, `reseq/*.h`, `reseq/*.hpp`
- Modify: `python/*.py`

- [ ] **Step 1: Format all project source files**

```bash
make format
```

- [ ] **Step 2: Verify build and tests still pass**

```bash
make test
```

- [ ] **Step 3: Commit as a dedicated formatting-only commit**

```bash
git add reseq/ python/
git commit -m "style: normalize formatting with clang-format and ruff

One-time bulk format to establish baseline. No logic changes.
Future commits will be enforced by pre-commit hooks and CI."
```

- [ ] **Step 4: Create `.git-blame-ignore-revs`**

Add the commit hash of the format commit:
```
# One-time formatting normalization
<commit-hash-from-step-3>
```

This tells `git blame` to skip the formatting commit.

```bash
git add .git-blame-ignore-revs
git commit -m "build: add .git-blame-ignore-revs for format commit"
```

---

## Task 8: clang-tidy Config

**Files:**
- Create: `.clang-tidy`

- [ ] **Step 1: Create `.clang-tidy`**

```yaml
---
Checks: >
  -*,
  bugprone-*,
  clang-analyzer-*,
  cppcoreguidelines-avoid-goto,
  cppcoreguidelines-no-malloc,
  misc-redundant-expression,
  misc-unused-using-decls,
  modernize-use-nullptr,
  modernize-use-override,
  modernize-loop-convert,
  modernize-use-emplace,
  performance-*,
  readability-braces-around-statements,
  readability-duplicate-include,
  readability-misleading-indentation,
  readability-redundant-string-cstr,
  -bugprone-easily-swappable-parameters,
  -bugprone-narrowing-conversions,
  -performance-avoid-endl
WarningsAsErrors: ''
HeaderFilterRegex: '.*/reseq/[^/]*\.(h|hpp)$'
FormatStyle: file
```

Note: `HeaderFilterRegex` uses `'.*/reseq/[^/]*\.(h|hpp)$'` to match absolute paths
from `compile_commands.json` while excluding vendored subdirectory headers.

- [ ] **Step 2: Run clang-tidy to see current state (informational)**

```bash
make lint || echo "Lint issues found — informational only at this stage"
```

- [ ] **Step 3: Commit**

```bash
git add .clang-tidy
git commit -m "build: add clang-tidy config (informational, not yet enforced)"
```

---

## Task 9: Pre-commit Hooks

**Files:**
- Create: `.pre-commit-config.yaml`

- [ ] **Step 1: Create `.pre-commit-config.yaml`**

```yaml
repos:
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v5.0.0
    hooks:
      - id: trailing-whitespace
        exclude: ^(seqan|googletest|nlopt|skewer|2016-05-15_ROOTPWA)/
      - id: end-of-file-fixer
        exclude: ^(seqan|googletest|nlopt|skewer|2016-05-15_ROOTPWA)/
      - id: check-yaml
      - id: check-added-large-files
        args: ['--maxkb=500']

  - repo: https://github.com/pocc/pre-commit-hooks
    rev: v1.3.5
    hooks:
      - id: clang-format
        args: ['--style=file', '-i']
        files: '^reseq/.*\.(cpp|h|hpp)$'

  - repo: https://github.com/astral-sh/ruff-pre-commit
    rev: v0.9.9
    hooks:
      - id: ruff
        files: '^python/.*\.py$'
      - id: ruff-format
        files: '^python/.*\.py$'
```

Note: clang-tidy is intentionally NOT in pre-commit — it requires a full build
and is too slow. It runs in CI instead.

- [ ] **Step 2: Install and test hooks**

```bash
pip install pre-commit
pre-commit install
pre-commit run --all-files
# Expected: all pass (codebase was normalized in Task 7)
```

- [ ] **Step 3: Commit**

```bash
git add .pre-commit-config.yaml
git commit -m "build: add pre-commit hooks for formatting enforcement"
```

---

## Task 10: GitHub Actions CI

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Create `.github/workflows/ci.yml`**

```yaml
name: CI

on:
  push:
    branches: [master, devel]
  pull_request:
    branches: [master]

jobs:
  build-and-test:
    strategy:
      fail-fast: false
      matrix:
        include:
          - compiler: gcc
            cc: gcc-13
            cxx: g++-13
            packages: gcc-13 g++-13
          - compiler: clang
            cc: clang-17
            cxx: clang++-17
            packages: clang-17
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y ${{ matrix.packages }} \
            libboost-all-dev libbz2-dev zlib1g-dev

      - name: Configure
        env:
          CC: ${{ matrix.cc }}
          CXX: ${{ matrix.cxx }}
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Test
        run: build/bin/reseq test

  format-check:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4

      - name: Install clang-format
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-format

      - name: Check C++ formatting
        run: |
          git ls-files 'reseq/*.cpp' 'reseq/*.h' 'reseq/*.hpp' \
            | xargs clang-format --dry-run --Werror

      - name: Check Python formatting
        run: |
          pip install ruff
          ruff check python/*.py
          ruff format --check python/*.py

  version-check:
    if: startsWith(github.ref, 'refs/tags/v')
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4

      - name: Verify tag matches VERSION file
        run: |
          TAG="${GITHUB_REF#refs/tags/v}"
          FILE_VERSION="$(cat VERSION | tr -d '[:space:]')"
          if [ "$TAG" != "$FILE_VERSION" ]; then
            echo "ERROR: Tag v$TAG does not match VERSION file ($FILE_VERSION)"
            exit 1
          fi
          echo "OK: Tag v$TAG matches VERSION file"
```

- [ ] **Step 2: Create directory and commit**

```bash
mkdir -p .github/workflows
git add .github/workflows/ci.yml
git commit -m "ci: add GitHub Actions workflow for build, test, and format check"
```

---

## Task 11: Changelog Tooling

**Files:**
- Create: `cliff.toml`

- [ ] **Step 1: Create `cliff.toml`**

```toml
[changelog]
header = """
# Changelog

All notable changes to ReSeq will be documented in this file.\n
"""
body = """
{% for group, commits in commits | group_by(attribute="group") %}
### {{ group | upper_first }}
{% for commit in commits %}
- {{ commit.message | split(pat="\\n") | first | trim }}\
{% endfor %}
{% endfor %}\n
"""
trim = true

[git]
conventional_commits = true
filter_unconventional = false
commit_parsers = [
    { message = "^feat", group = "Features" },
    { message = "^fix", group = "Bug Fixes" },
    { message = "^perf", group = "Performance" },
    { message = "^refactor", group = "Refactoring" },
    { message = "^doc", group = "Documentation" },
    { message = "^test", group = "Testing" },
    { message = "^build|^ci", group = "Build/CI" },
    { message = "^style", group = "Style" },
    { message = ".*", group = "Other" },
]
```

- [ ] **Step 2: Add changelog target to `Makefile`**

Append to Makefile before the `help` target:
```makefile
changelog:
	git-cliff --output CHANGELOG.md
```

Update the help target to include:
```
	@echo "  changelog    Generate CHANGELOG.md from git history"
```

- [ ] **Step 3: Generate initial changelog (if git-cliff is installed)**

```bash
git-cliff --output CHANGELOG.md || echo "git-cliff not installed, skipping"
```

- [ ] **Step 4: Commit**

```bash
git add cliff.toml Makefile
git add CHANGELOG.md 2>/dev/null  # only if generated
git commit -m "build: add git-cliff config for changelog generation"
```

---

## Self-Review Checklist

1. **Formatting contradiction resolved:** Task 7 normalizes the codebase BEFORE Task 9 (hooks) and Task 10 (CI) enforce formatting. No chicken-and-egg problem.

2. **CMake floor preserved:** Task 4 keeps `cmake_minimum_required(VERSION 3.1)` and parses VERSION manually with `file(READ)` + `string(REPLACE)`. No `project(VERSION ${var})` which requires 3.12+.

3. **Python floor preserved:** `pyproject.toml` uses `target-version = "py36"`. No `UP` (pyupgrade) rules. No f-strings in existing code.

4. **File extensions correct:** All format/lint scopes include `*.hpp` alongside `*.cpp` and `*.h`. The 5 `.hpp` files in `reseq/` are covered: `utilities.hpp`, `Vect.hpp`, `SeqQualityStats.hpp`, `SurroundingBase.hpp`, `BasicTestClass.hpp`.

5. **HeaderFilterRegex uses absolute path pattern:** `.*/reseq/[^/]*\.(h|hpp)$` matches absolute paths from `compile_commands.json`.

6. **Python bindings solved in CMake:** Task 5 adds `option(RESEQ_BUILD_PYTHON)` defaulting to OFF. When ON, `python/CMakeLists.txt` emits `FATAL_ERROR` if SWIG or Python is missing — no silent no-op.

7. **All Python files accounted for:** `reseq-prepare-names.py`, `plotDataStats.py` are linted. `DataStats.i` (SWIG) excluded.

8. **No vendored modifications:** Task 2 only creates root `.clang-format`. No files created/modified in vendored dirs. `googletest/.clang-format` (upstream) left untouched. Scope enforcement is handled by explicit file enumeration in Makefile, hooks, and CI.

9. **CI installs its toolchain:** Build matrix includes `packages` field with compiler packages. Format-check job explicitly installs `clang-format`.

10. **File enumeration is robust:** Makefile uses `git ls-files` (null-safe, only tracked files). CI uses `git ls-files | xargs`. Pre-commit hooks use regex `files:` patterns. No brittle `find | xargs` without grouping.

11. **No placeholders:** Every task has exact file contents, exact commands, expected output.
