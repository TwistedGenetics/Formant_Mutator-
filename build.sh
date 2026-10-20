#!/usr/bin/env bash
# Build FORMANT MUTATOR for the Korg NTS-1 mkII without Docker.
# Downloads the ARM GCC 10.3 toolchain (xPack) and the official logue SDK on
# first run, then builds dist/FormantMutator.nts1mkiiunit.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
DEPS="$ROOT/.deps"
SDK_DIR="$DEPS/logue-sdk"
SDK_REPO="https://github.com/korginc/logue-sdk"
SDK_COMMIT="e78b16c6068b1c30fc072f2aebb8da5eef6c005c"
GCC_VER="10.3.1-2.3"

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)  GCC_PLAT="linux-x64" ;;
  Linux-aarch64) GCC_PLAT="linux-arm64" ;;
  Darwin-arm64)  GCC_PLAT="darwin-arm64" ;;
  Darwin-x86_64) GCC_PLAT="darwin-x64" ;;
  *) echo "Unsupported platform: $(uname -s)-$(uname -m)"; exit 1 ;;
esac

GCC_DIR="$DEPS/xpack-arm-none-eabi-gcc-$GCC_VER"
GCC_URL="https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$GCC_VER/xpack-arm-none-eabi-gcc-$GCC_VER-$GCC_PLAT.tar.gz"

mkdir -p "$DEPS"

if [ ! -x "$GCC_DIR/bin/arm-none-eabi-gcc" ]; then
  echo "==> Downloading ARM GCC $GCC_VER ($GCC_PLAT)"
  curl -fsSL "$GCC_URL" | tar xz -C "$DEPS"
fi

if [ ! -d "$SDK_DIR/platform/nts-1_mkii" ]; then
  echo "==> Fetching logue SDK"
  git clone -q "$SDK_REPO" "$SDK_DIR"
  git -C "$SDK_DIR" checkout -q "$SDK_COMMIT"
  git -C "$SDK_DIR" submodule update -q --init --depth 1 platform/ext/CMSIS
fi

PLAT="$SDK_DIR/platform/nts-1_mkii"
mkdir -p "$ROOT/dist"

echo "==> Building FORMANT MUTATOR"
make -C "$ROOT/formant_mutator" ${1:-install} \
  GCC_BIN_PATH="$GCC_DIR/bin" \
  COMMON_INC_PATH="$PLAT/common" \
  COMMON_SRC_PATH="$PLAT/common" \
  EXTDIR="$SDK_DIR/platform/ext" \
  LDDIR="$PLAT/ld" \
  INSTALLDIR="$ROOT/dist"

if [ "${1:-install}" = "install" ]; then
  mv -f "$ROOT/dist/formant_mutator.nts1mkiiunit" "$ROOT/dist/FormantMutator.nts1mkiiunit"
  echo "==> Done: dist/FormantMutator.nts1mkiiunit"
fi
