# Contributing

- Trunk-based: branch from `main`, open a PR, squash-merge. `main` is protected and must stay green.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat(alarm): ...`, `fix(display): ...`). release-please builds the changelog and version from them.
- Logic that does not touch hardware goes in `firmware/lib/` and gets a test in `firmware/test/`. Run `pio test -e native` before pushing.
- Format C++ with `clang-format` (config in repo root).
- Architecture changes start as an ADR in `docs/adr/` (copy `0000-template.md`).
- Enclosure changes: keep everything parametric; no hardcoded board dimensions outside the parameter block.
