# Building and installing FORMANT MUTATOR

The project uses the **official** Korg logue SDK build system ([korginc/logue-sdk](https://github.com/korginc/logue-sdk), `platform/nts-1_mkii`). It targets the NTS-1 mkII (SDK 2.0 unit format) only. It is **not** compatible with the original NTS-1 or with first-generation logue units.

Requirements set by the SDK:
- Arm GNU toolchain **gcc-arm-none-eabi 10.3-2021.10**
- GNU Make
- The CMSIS submodule (`platform/ext/CMSIS`)
- NTS-1 mkII firmware ≥ 1.0.0 (per Korg's SDK README)

The SDK's own build produces `formant_mutator.nts1mkiiunit`; `build.sh` and Releases rename it to `FormantMutator.nts1mkiiunit`.

---

## Option A: GitHub Releases (nothing to build)

Every push to `main` runs `.github/workflows/build.yml`. It builds the unit with the official SDK, runs the DSP tests, and publishes a **GitHub Release** named after the version in `header.c` (`.version = 0x00010000` → `v1.0.0`). The release carries `FormantMutator.nts1mkiiunit` and `FormantMutator-preset-renders.zip`. If the version is unchanged, re-runs update the existing release's files.

**To ship a new version:** bump `.version` in `formant_mutator/header.c` (e.g. `0x00010100` for 1.1.0) and push to `main`.

## Option A2: `./build.sh` (Linux, macOS, WSL; no Docker)

```bash
./build.sh            # → dist/FormantMutator.nts1mkiiunit
./test/run.sh 600     # optional: DSP tests + WAV renders in test/out/
```

On first run, `build.sh` downloads the xPack build of Arm GCC 10.3.1 (the same GCC release Korg specifies) and the official logue SDK pinned to a known commit, into `.deps/`. On Windows, run it inside WSL (Ubuntu).

## Option B: Windows with Docker Desktop (Korg's official container)

Korg's Docker notes for Windows require **WSL 2** and say the helper scripts must run from a **WSL bash prompt** (`wsl` or `bash` in PowerShell). Git Bash lacks the TTY support `run_cmd.sh` and `run_interactive.sh` need.

1. Install **WSL 2** with Ubuntu, and **Docker Desktop** using the WSL 2 backend.
2. In PowerShell run `wsl`, then inside that Ubuntu shell:
   ```bash
   git config --global core.autocrlf input
   git clone https://github.com/korginc/logue-sdk.git
   cd logue-sdk
   git submodule update --init
   ```
3. Build Korg's container (one-time, a few minutes):
   ```bash
   cd docker && ./build_image.sh && cd ..
   ```
   (Korg's README also mentions a community image, `docker pull xiashj/logue-sdk`.)
4. Copy the `formant_mutator` folder from this repo into `logue-sdk/platform/nts-1_mkii/`, so you have `logue-sdk/platform/nts-1_mkii/formant_mutator/Makefile`.
5. Build:
   ```bash
   docker/run_cmd.sh build nts-1_mkii/formant_mutator
   ```
   or interactively:
   ```bash
   docker/run_interactive.sh
   build nts-1_mkii/formant_mutator
   ```
6. The unit appears at `logue-sdk/platform/nts-1_mkii/formant_mutator/formant_mutator.nts1mkiiunit`. From Windows Explorer it is under `\\wsl$\Ubuntu\home\<you>\logue-sdk\...`.

To clean: `docker/run_cmd.sh build --clean nts-1_mkii/formant_mutator`.

## Option C: Linux / macOS / WSL without Docker (legacy method)

```bash
git clone https://github.com/korginc/logue-sdk.git
cd logue-sdk && git submodule update --init
tools/gcc/get_gcc_10_3-2021_10_linux.sh      # or ..._macos.sh
cp -r /path/to/this/repo/formant_mutator platform/nts-1_mkii/
cd platform/nts-1_mkii/formant_mutator
make && make install
```

If your toolchain is installed elsewhere, point the SDK Makefile at it:
`make GCC_BIN_PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin`.

`build.sh` and the Releases use the xPack build of the same GCC release (`xpack-arm-none-eabi-gcc 10.3.1-2.3`, GCC 10.3.1 20210824), the same setup as VectorFM.

## Running the offline test harness (any OS with a C++ compiler)

```bash
./test/run.sh 600     # full suite incl. 10-minute soak; writes test/out/*.wav + test_report.txt
./test/run.sh         # same, 60-second soak  (or: make -C test run / quick)
```

On Windows, use WSL, MSYS2 (`pacman -S make mingw-w64-x86_64-gcc`) or the GitHub Actions run.

---

## Installing on the NTS-1 mkII

1. Open **KORG KONTROL Editor** with the NTS-1 mkII connected over USB.
2. Drag `FormantMutator.nts1mkiiunit` onto the **oscillator** list, then press **SEND**.
3. On the NTS-1 mkII, select FORMANT MUTATOR with the OSC button.

Korg's SDK README also names **loguecli** as a loader.

### Unit identity
- Developer ID: `0x5477476E` ('TwGn'). This is **not registered** in Korg's `developer_ids.md`, which Korg says is currently enforced only for drumlogue. To make it official, open a PR on logue-sdk adding the ID, and use the same ID for all your units (VectorFM etc.).
- Unit ID: `0x00000002`. It must be unique among your units. If VectorFM already uses 2, change `.unit_id` in `header.c`.
- Version: 1.0.0
