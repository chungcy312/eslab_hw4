#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="$ROOT_DIR/.deps/STM32Cube_FW_L4_V1.18.2"
SDK_REPO="https://github.com/STMicroelectronics/STM32CubeL4.git"
SDK_TAG="v1.18.2"

usage() {
  printf 'Usage: %s [all|stm32|rpi]\n' "$0"
  printf '  all    Prepare the local BLE Python environment and build STM32 firmware (default)\n'
  printf '  stm32  Install/find the ARM toolchain, fetch STM32CubeL4, and build firmware\n'
  printf '  rpi    Install Raspberry Pi BLE runtime requirements and create the Python venv\n'
}

find_cubeide_toolchain() {
  local found
  found="$(find /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins \
    -type f -path '*/tools/bin/arm-none-eabi-gcc' -print -quit 2>/dev/null || true)"
  if [[ -n "$found" ]]; then dirname "$found"; fi
}

prepare_stm32() {
  local toolchain_bin=""
  mkdir -p "$ROOT_DIR/.deps"

  if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    if [[ "$(uname -s)" == "Darwin" ]]; then
      if command -v brew >/dev/null 2>&1; then
        brew install --formula arm-none-eabi-gcc
      else
        toolchain_bin="$(find_cubeide_toolchain)"
        if [[ -z "$toolchain_bin" ]]; then
          printf 'Install STM32CubeIDE or Homebrew arm-none-eabi-gcc first.\n' >&2
          return 1
        fi
      fi
    elif command -v apt-get >/dev/null 2>&1; then
      sudo apt-get update
      sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi make git
    else
      printf 'Install arm-none-eabi-gcc, make, and git for this operating system.\n' >&2
      return 1
    fi
  fi

  if [[ ! -d "$SDK_DIR/Drivers" ]]; then
    git clone --depth 1 --recurse-submodules --shallow-submodules \
      --branch "$SDK_TAG" "$SDK_REPO" "$SDK_DIR"
  fi

  if [[ -n "$toolchain_bin" ]]; then
    make -C "$ROOT_DIR/stm32" clean all SDK_ROOT="$SDK_DIR" TOOLCHAIN="$toolchain_bin"
  else
    make -C "$ROOT_DIR/stm32" clean all SDK_ROOT="$SDK_DIR"
  fi
  printf '\nSTM32 build complete: %s/stm32/build/eslab_hw4.elf\n' "$ROOT_DIR"
}

prepare_rpi() {
  if [[ "$(uname -s)" == "Linux" ]] && command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y python3 python3-venv python3-pip bluez bluetooth
  fi

  python3 -m venv "$ROOT_DIR/rpi/.venv"
  "$ROOT_DIR/rpi/.venv/bin/python" -m pip install --upgrade pip
  "$ROOT_DIR/rpi/.venv/bin/python" -m pip install -r "$ROOT_DIR/rpi/requirements.txt"
  printf '\nRPi BLE environment ready. Run:\n  %s/rpi/.venv/bin/python %s/rpi/demo.py --sequence\n' \
    "$ROOT_DIR" "$ROOT_DIR"
}

MODE="${1:-all}"
case "$MODE" in
  all)
    prepare_rpi
    prepare_stm32
    ;;
  stm32) prepare_stm32 ;;
  rpi) prepare_rpi ;;
  -h|--help|help) usage ;;
  *) usage >&2; exit 2 ;;
esac
