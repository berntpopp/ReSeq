# CLAUDE.md

## Repository

This is a fork (berntpopp/ReSeq) of upstream (schmeing/ReSeq).
The `upstream` remote has been removed — only `origin` (berntpopp/ReSeq) exists.

## Rules

- **All work stays in the fork**: All PRs, pushes, and branches target `berntpopp/ReSeq` only.
- **Never interact with upstream**: Do not add the upstream remote, create PRs on `schmeing/ReSeq`, or push to it. Ever.
- **Use `--repo berntpopp/ReSeq`** with all `gh` commands.

## Build

- `make build` — configure and build
- `make test` — build and run 24 unit tests
- `make format` — format C++ (clang-format) and Python (ruff)
- `make format-check` — dry-run format check
- `make lint` — clang-tidy and ruff
- `pre-commit run --all-files` — run all pre-commit hooks
