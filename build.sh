#!/usr/bin/env bash
# Build FORMANT MUTATOR without Docker.
#   ./build.sh          NTS-1 mkII and microKORG 2 units
#   ./build.sh nts1     NTS-1 mkII only  -> dist/FormantMutator.nts1mkiiunit
#   ./build.sh mk2      microKORG 2 only -> dist/FormantMutator.mk2unit
# Downloads the toolchains and the official logue SDK into .deps/ on first run:
#   NTS-1 mkII : ARM GCC 10.3 (xPack build of the release Korg specifies)
#   microKORG 2: Korg's own arm-unknown-linux-gnueabihf toolchain (the one
#                logue-sdk/docker installs; Linux x86_64 / aarch64 only)
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

TARGET="${1:-all}"
mkdir -p "$DEPS" "$ROOT/dist"

if [ ! -d "$SDK_DIR/platform/nts-1_mkii" ]; then
  echo "==> Fetching logue SDK"
  git clone -q "$SDK_REPO" "$SDK_DIR"
  git -C "$SDK_DIR" checkout -q "$SDK_COMMIT"
  git -C "$SDK_DIR" submodule update -q --init --depth 1 platform/ext/CMSIS
fi

build_nts1() {
  GCC_DIR="$DEPS/xpack-arm-none-eabi-gcc-$GCC_VER"
  GCC_URL="https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$GCC_VER/xpack-arm-none-eabi-gcc-$GCC_VER-$GCC_PLAT.tar.gz"

  if [ ! -x "$GCC_DIR/bin/arm-none-eabi-gcc" ]; then
    echo "==> Downloading ARM GCC $GCC_VER ($GCC_PLAT)"
    curl -fsSL "$GCC_URL" | tar xz -C "$DEPS"
  fi

  local PLAT="$SDK_DIR/platform/nts-1_mkii"
  echo "==> Building FORMANT MUTATOR for NTS-1 mkII"
  make -C "$ROOT/formant_mutator" install \
    GCC_BIN_PATH="$GCC_DIR/bin" \
    COMMON_INC_PATH="$PLAT/common" \
    COMMON_SRC_PATH="$PLAT/common" \
    EXTDIR="$SDK_DIR/platform/ext" \
    LDDIR="$PLAT/ld" \
    INSTALLDIR="$ROOT/dist"
  mv -f "$ROOT/dist/formant_mutator.nts1mkiiunit" "$ROOT/dist/FormantMutator.nts1mkiiunit"
  echo "==> Done: dist/FormantMutator.nts1mkiiunit"
}

build_mk2() {
  # Same archive, URL and SHA1 as logue-sdk/docker/docker-app/builder/get_microkorg2_toolchain.sh
  case "$(uname -s)-$(uname -m)" in
    Linux-x86_64)
      MK2_URL="https://www.dropbox.com/s/3curhjx9yip29os/drumlogue-toolchain-amd64.tar.bz2"
      MK2_SHA1="6b2a403195fbac0f7ee8d53bea4ed17b8c14e541" ;;
    Linux-aarch64)
      MK2_URL="https://www.dropbox.com/s/ioysa018ruopp72/drumlogue-toolchain-aarch64.tar.bz2"
      MK2_SHA1="d907b640967e8ea40baa82ff55d8813716028aa9" ;;
    *)
      echo "==> Skipping microKORG 2: Korg's toolchain is Linux-only (use WSL, Docker or GitHub Actions)"
      return 0 ;;
  esac
  local TC="$DEPS/mk2-toolchain"
  if [ -z "$(find "$TC" -name arm-unknown-linux-gnueabihf-gcc \( -type f -o -type l \) 2>/dev/null | head -1)" ]; then
    echo "==> Downloading Korg microKORG 2 toolchain"
    mkdir -p "$TC"
    curl -fsSL -o "$DEPS/mk2tc.tar.bz2" "$MK2_URL" || curl -fsSL -o "$DEPS/mk2tc.tar.bz2" "$MK2_URL?dl=1"
    echo "$MK2_SHA1  $DEPS/mk2tc.tar.bz2" | sha1sum -c -
    tar xjf "$DEPS/mk2tc.tar.bz2" -C "$TC"
    rm -f "$DEPS/mk2tc.tar.bz2"
  fi
  local GCC
  GCC="$(find "$TC" -name arm-unknown-linux-gnueabihf-gcc \( -type f -o -type l \) | head -1)"
  local PLAT="$SDK_DIR/platform/microkorg2"
  echo "==> Building FORMANT MUTATOR for microKORG 2"
  make -C "$ROOT/microkorg2" install \
    CROSS_COMPILE="$(dirname "$GCC")/arm-unknown-linux-gnueabihf-" \
    COMMON_INC_PATH="$PLAT/common" \
    COMMON_SRC_PATH="$PLAT/common" \
    PLATFORM_COMMON_PATH="$SDK_DIR/platform/common" \
    INSTALLDIR="$ROOT/dist"
  mv -f "$ROOT/dist/formant_mutator.mk2unit" "$ROOT/dist/FormantMutator.mk2unit"
  echo "==> Done: dist/FormantMutator.mk2unit"

  echo "==> Building FMdiag (microKORG 2 diagnostic unit)"
  make -C "$ROOT/tools/mk2-diag" install \
    CROSS_COMPILE="$(dirname "$GCC")/arm-unknown-linux-gnueabihf-" \
    COMMON_INC_PATH="$PLAT/common" \
    COMMON_SRC_PATH="$PLAT/common" \
    PLATFORM_COMMON_PATH="$SDK_DIR/platform/common" \
    INSTALLDIR="$ROOT/dist"
  mv -f "$ROOT/dist/fmdiag.mk2unit" "$ROOT/dist/FMutator-diagnostic.mk2unit"
  echo "==> Done: dist/FMutator-diagnostic.mk2unit"
}

case "$TARGET" in
  nts1) build_nts1 ;;
  mk2)  build_mk2 ;;
  all)  build_nts1; build_mk2 ;;
  *) echo "usage: $0 [all|nts1|mk2]"; exit 1 ;;
esac
