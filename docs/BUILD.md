# Building and installing FORMANT MUTATOR

The project uses the **official** Korg logue SDK build system ([korginc/logue-sdk](https://github.com/korginc/logue-sdk), `platform/nts-1_mkii`). It targets the NTS-1 mkII (SDK 2.0 unit format) only. It is **not** compatible with the original NTS-1 or with first-generation logue units.

Requirements set by the SDK:
- Arm GNU toolchain **gcc-arm-none-eabi 10.3-2021.10**
- GNU Make
- The CMSIS submodule (`platform/ext/CMSIS`)
- NTS-1 mkII firmware ≥ 1.0.0 (per Korg's SDK README)

The build output is `formant_mutator.nts1mkiiunit`.

---

## Option A: GitHub Actions (no local setup, recommended on Windows)

This repository includes `.github/workflows/build.yml`. On every push, or when run manually from the **Actions** tab, it:

1. clones `korginc/logue-sdk` and initialises the CMSIS submodule
2. downloads Arm GCC 10.3-2021.10 (Korg's own `tools/gcc` download URL)
3. copies `formant_mutator/` into `logue-sdk/platform/nts-1_mkii/`
4. runs `make` and `make install`
5. builds and runs the offline test harness
6. uploads `formant_mutator.nts1mkiiunit`, the test report and the WAV renders as a build artifact

To use it: open the repo on GitHub → **Actions** → **Build NTS-1 mkII unit** → **Run workflow**, then download the artifact ZIP from the finished run.

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

The release binary in this repo was built with the Legacy method using the xPack build of the same GCC release (`xpack-arm-none-eabi-gcc 10.3.1-2.3`, GCC 10.3.1 20210824), because the build machine could not reach developer.arm.com.

## Running the offline test harness (any OS with a C++ compiler)

```bash
make -C test run      # full suite incl. 10-minute soak; writes test/out/*.wav + test_report.txt
make -C test quick    # same, 60-second soak
```

On Windows, use WSL, MSYS2 (`pacman -S make mingw-w64-x86_64-gcc`) or the GitHub Actions run.

---

## Installing on the NTS-1 mkII

Korg's SDK README names two loaders for `.nts1mkiiunit` files: **KORG Kontrol Editor** (with NTS-1 mkII support) and **loguecli**. With Kontrol Editor:

1. Connect the NTS-1 mkII over USB and open KORG Kontrol Editor.
2. Open the user oscillator slots, drag `formant_mutator.nts1mkiiunit` onto a free slot, and send/write it to the device.
3. On the NTS-1 mkII, select the user oscillator **FORMANT MUTATOR**.

Exact menu names depend on your Kontrol Editor version. Korg's NTS-1 mkII product page lists its current version.

### Unit identity
- Developer ID: `0x5477476E` ('TwGn'). This is **not registered** in Korg's `developer_ids.md`, which Korg says is currently enforced only for drumlogue. To make it official, open a PR on logue-sdk adding the ID, and use the same ID for all your units (VectorFM etc.).
- Unit ID: `0x00000002`. It must be unique among your units. If VectorFM already uses 2, change `.unit_id` in `header.c`.
- Version: 1.0.0
