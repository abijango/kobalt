# Working on Kobalt public source

Read [README.md](README.md) before changing code. Follow [Building Kobalt](docs/development.md) for build commands, tests and source boundaries. [Shared reader controls](docs/development.md#shared-reader-controls) is the canonical component guidance. Keep reader behavior documentation current in [Reader behavior](docs/reader.md).

## README edits

The README must be edited manually.

## Release version

When preparing a release, update the root `VERSION` file to the release tag, such as `v1.0-beta.2`, in the same change as its `CHANGELOG.md` entry. Check that both match before tagging. Untagged builds use `VERSION`; tagged CI builds use the triggering tag.

## Acknowledgements

Update [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) when adding or replacing a dependency or asset. Include upstream links and the original authors. Credit Nico Verbruggen for his projects and adaptations, while keeping the original upstream authors credited. Keep maintenance instructions in this file, not in the acknowledgements document.

## Cursor Cloud specific instructions

The image provides the pinned host Qt 6.5 SDK at `/opt/kobalt/host/qt` and verified dependency archives in `/opt/kobalt/cache`. Setup links those paths to `build/tests/linux-<arch>/qt` and `.cache/kobalt`, so the documented host command is unchanged:

```sh
python3 tools/run_tests.py --jobs 2
```

`clang-format` 18.1.8 is on `PATH`. Check style with `python3 tools/format_cpp.py --check`. The default `c++` is Clang 18, and `g++-14` plus `libstdc++-14-dev` are installed so that compiler can link `libstdc++`. Host tests and `python3 tools/native_screenshots.py` use Qt's offscreen platform. Boot enables an 8 GiB `/swapfile` for the Qt and engine builds. Device packages use `python3 tools/ci_build.py` with a container engine; this environment does not start one.
