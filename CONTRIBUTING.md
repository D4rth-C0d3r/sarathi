# Contributing to sārathi

Thanks for your interest. sārathi is a small hobby project, so please open an issue to
discuss larger changes before you start on them.

By taking part you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Setup

```bash
git clone --recurse-submodules --shallow-submodules https://github.com/D4rth-C0d3r/sarathi.git
cd sarathi
tools/setup.sh .               # PlatformIO, project .venv, submodule
source .venv/bin/activate
pre-commit install             # run format and lint hooks on every commit
```

Repo Python tools run from the project `.venv` only. Add their dependencies to
`tools/requirements.txt`; don't install packages globally.

## Making a change

1. Fork and create a branch from `main`.
2. Keep each change small. Every commit should build and pass the tests on its own.
3. Before opening a PR, run the checks for the area you touched:

   ```bash
   pre-commit run --all-files

   # firmware/
   pio run -e crowpanel21
   pio test -e native

   # android/
   ./gradlew lint testDebugUnitTest ktlintCheck detekt
   ```

4. If you changed behaviour on the device, say in the PR what you tested on real hardware.
5. Update the docs in the same PR as the code (`docs/building.md`, `docs/hardware.md` or a
   new ADR in `docs/adr/` for significant decisions).

## Commit messages

We use [Conventional Commits](https://www.conventionalcommits.org/). The changelog and
version numbers are generated from them.

```
feat(firmware): show next-turn distance
fix(android): reconnect after Bluetooth toggle
docs(protocol): add media_cmd seek field
```

Common scopes: `firmware`, `android`, `protocol`, `docs`, `ci`, `tools`.

## Project rules

- **Architecture.** The device is BLE only. Don't propose designs that need Classic
  Bluetooth audio on the device. See ADR-0001.
- **Protocol.** For any change to the phone ↔ device protocol, update
  `docs/protocol/protocol.md`, `schema/` and `vectors/` first. Firmware and Android tests
  run against the same vectors.
- **Hardware.** Pins, I2C addresses and panel timings live only in
  `firmware/lib/sarathi_board/board_pins.h`, and each must cite its source. Never guess one.
- **vendor/** is read-only. Copy what you need into `sarathi_board`, keeping attribution.
- **Dependencies.** Toolchain and library versions are pinned. Upgrade them in a dedicated PR,
  after testing on the device.
- **Rider safety.** Everything must work with the knob alone. Keep the UI readable at a
  glance, and don't add features that encourage interacting with the device while riding.
- **Secrets.** Never commit signing keys or other credentials.

## Code style

- **C++:** C++17, formatted with `.clang-format` (Google base, 100 columns) and checked with
  `.clang-tidy`.
- **Kotlin:** ktlint and detekt.
- Editor basics come from `.editorconfig`.

## License

By contributing, you agree that your contributions are licensed under the
[Apache License 2.0](LICENSE).
