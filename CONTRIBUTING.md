# Contributing

Thanks for looking. Bug reports, ideas and pull requests are all welcome.

## Ideas and questions

- Feature ideas and questions: [Discussions](https://github.com/braveadmin/inkwake/discussions)
  ("Ideas" category). Once an idea is clear it becomes an issue.
- Bugs: open an issue with the bug template. Serial output (115200 baud, `log` command) helps.
- Scope and priorities: [docs/features.md](docs/features.md), plan: [docs/roadmap.md](docs/roadmap.md).

## No board? You can still help

The board is only needed for the drivers in `firmware/src/`. Everything else runs on a PC:

- **Logic and tests.** Time zones, alarm scheduling, NTP, Wi-Fi list, battery levels and the
  display layout live in `firmware/lib/` and are tested with `pio test -e native`. Without
  PlatformIO they also build with plain g++ and Unity (see `CLAUDE.md`, Conventions).
- **Screens.** `firmware/tools/preview_face.cpp` draws the clock face, setup and ringing screens
  into a PBM; `firmware/tools/pbm2png.py` turns it into a PNG. Attach before/after images to PRs
  that change the layout.
- **Setup page.** `firmware/web/index.html` is a plain HTML page; `firmware/tools/gen_web.py`
  packs it into `src/web_page.h` (CI checks they are in sync).
- **Docs**, the enclosure (OpenSCAD) and the simulator (`sim/`, M6) need no board either.

Issues labelled [good first issue](https://github.com/braveadmin/inkwake/labels/good%20first%20issue)
or [help wanted](https://github.com/braveadmin/inkwake/labels/help%20wanted) are a good start.
Comment on the issue before starting so two people don't do the same work.

## Rules

- Trunk-based: branch from `main`, open a PR, squash-merge. `main` is protected and must stay green.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/) (`feat(alarm): ...`, `fix(display): ...`). release-please builds the changelog and version from them.
- Logic that does not touch hardware goes in `firmware/lib/` and gets a test in `firmware/test/`. Run `pio test -e native` before pushing.
- Format C++ with `clang-format` 18 (config in repo root; `pip install clang-format==18.1.3`).
- Architecture changes start as an ADR in `docs/adr/` (copy `0000-template.md`).
- Enclosure changes: keep everything parametric; no hardcoded board dimensions outside the parameter block.
