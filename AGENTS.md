# Working on Kobalt public source

Read [README.md](README.md) before changing code. Follow [Building Kobalt](docs/development.md) for build commands, tests and source boundaries. [Shared reader controls](docs/development.md#shared-reader-controls) is the canonical component guidance. Keep reader behavior documentation current in [Reader behavior](docs/reader.md).

## README edits

Only the user may edit the root `README.md`. Agents must not modify it. If a change requires a README update, propose the required text to the user.

## Worktrees and branches

For new implementation work, use a separate Git worktree on a separate branch based on `main` (or `develop` if it exists and is ahead). When continuing existing work, use its existing worktree and branch unless the user requests otherwise. When the work is complete and verified, prepare it for merging back into that branch. This workflow does not authorize staging, committing or merging without the user's explicit instruction.

## Key constraints

- Design for slow ARM cores, limited memory and e-paper displays. Follow [Performance and resource use](docs/development.md#performance-and-resource-use) and support performance claims with measurements.
- Keep shared reader behavior compatible with Kobo Qt5, Kobo Qt6, Android and the macOS development app. Firmware 4 uses Qt5; firmware 5 and 6 use Qt6. Use firmware 4 and Qt5 for Kobo builds and interactive previews unless the user requests another version.
- Before adding a control or interaction, look for an existing shared reader component. Follow its ownership, layout and interaction rules. Propose changes to established UI behavior before implementing them.
- Preserve existing functionality when adding features or refactoring. Check affected callers and platform implementations before changing shared contracts.
- Public builds and tests must work from this checkout alone. Do not add dependencies on private checkouts, private fixtures or local services.

## Verification

Add regression tests for bugs you fix. Run the relevant checks from [Building Kobalt](docs/development.md), including the formatting and UI architecture checks when they apply. State which checks were skipped and why. For build velocity, it may be useful to run tests selectively, as tests can take a while.

For UI changes, inspect affected screenshots in light and dark mode at the documented densities. Preserve the affected capture set before a UI refactor, as described in [Native screenshot tour](docs/development.md#native-screenshot-tour). A successful capture does not replace visual inspection.

Report source inspection, host tests, platform integration checks and physical-device checks separately. Host tests do not establish Nickel ABI compatibility or physical-device display behavior.

## Note on Nickel 6.x

Firmware 6 Nickel can abort with heap corruption while its process exits, after `ServiceLocator::~ServiceLocator`, with `malloc_consolidate(): unaligned fastbin chunk detected` or `corrupted double-linked list`. This happens with any NickelHook plugin loaded, including one with no hooks and no Kobalt code. The cause is not known. It happens only at process exit, after Kobalt has saved its state. Since Nickel 6.x isn't considered stable yet, treat it as a known issue, not as a regression or a release blocker, unless a change makes it happen before exit or more often.

## Platforms

- Intended release platforms are Kobo with Qt5 or Qt6, and Android. This does not mean every target is ready for release; see the README for current availability.
- macOS is a development platform for the shared reader, not a general release target.
- Tolino has partial support through Qt6 but is not a supported release target.
- iOS/iPadOS, PocketBook and jailbroken Kindle are under consideration and are not supported.

## Release validation

Before a release is tagged, do the following:

- Make sure the changelog contains all new user-facing changes (not technical decisions, minor changes).
- When preparing a release, update the root `VERSION` file to the release tag, such as `v1.0-beta.2`, in the same change as its `CHANGELOG.md` entry. Check that both match before tagging. Untagged builds use `VERSION`; tagged CI builds use the triggering tag.
- Assemble the screenshot pass for the previous tag and compare it to the new release. Did all the differences match expected changes in the codebase, or did something unexpected change?

## Acknowledgements

Update [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) when adding or replacing a dependency or asset. Include upstream links and the original authors. Credit Nico Verbruggen for his projects and adaptations, while keeping the original upstream authors credited. Keep maintenance instructions in this file, not in the acknowledgements document.

## Cursor Cloud specific instructions

The image provides the pinned host Qt 6.5 SDK at `/opt/kobalt/host/qt` and verified dependency archives in `/opt/kobalt/cache`. Setup links those paths to `build/tests/linux-<arch>/qt` and `.cache/kobalt`, so the documented host command is unchanged:

```sh
python3 tools/run_tests.py --jobs 2
```

`clang-format` 18.1.8 is on `PATH`. Check style with `python3 tools/format_cpp.py --check`. The default `c++` is Clang 18, and `g++-14` plus `libstdc++-14-dev` are installed so that compiler can link `libstdc++`. Host tests and `python3 tools/native_screenshots.py` use Qt's offscreen platform. Boot enables an 8 GiB `/swapfile` for the Qt and engine builds. Device packages use `python3 tools/ci_build.py` with a container engine; this environment does not start one.
