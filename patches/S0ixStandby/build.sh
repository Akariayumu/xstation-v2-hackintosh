#!/usr/bin/env bash
# Reproducible macOS Release build script for S0ixStandby 0.5.4 + X-STATION V2 v2 patch
# (-s0ixacnoecwake + read-only Gen12 GPU MHz export via setProperties)
#
# Usage (on macOS with Xcode Command Line Tools installed):
#   ./build.sh <path-to-S0ixStandby-0.5.4-src> [path-to-MacKernelSDK]
#
# Example:
#   git clone --branch main https://github.com/labrouss/S0ixStandby /tmp/S0ixStandby
#   git -C /tmp/S0ixStandby checkout 28069d3
#   git clone --depth 1 https://github.com/acidanthera/MacKernelSDK /tmp/S0ixStandby/MacKernelSDK
#   ./build.sh /tmp/S0ixStandby /tmp/S0ixStandby/MacKernelSDK
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_FILE="${SCRIPT_DIR}/s0ixstandby-0.5.4-xstation-v2.patch"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <S0ixStandby-src-dir> [MacKernelSDK-dir]" >&2
    exit 1
fi

SRC_DIR="$(cd "$1" && pwd)"
SDK_DIR="${2:-${SRC_DIR}/MacKernelSDK}"

if [[ ! -f "${SRC_DIR}/Makefile" || ! -f "${SRC_DIR}/S0ixStandby/S0ixStandby.cpp" ]]; then
    echo "Error: ${SRC_DIR} does not look like S0ixStandby 0.5.4 source tree" >&2
    exit 1
fi

if [[ ! -d "${SDK_DIR}/Headers" || ! -f "${SDK_DIR}/Library/x86_64/libkmod.a" ]]; then
    echo "Error: MacKernelSDK not found at ${SDK_DIR}" >&2
    exit 1
fi

if grep -q -- "refreshGpuFreq" "${SRC_DIR}/S0ixStandby/S0ixStandby.cpp"; then
    echo "[1/3] Patch already applied."
elif grep -q -- "-s0ixacnoecwake" "${SRC_DIR}/S0ixStandby/S0ixStandby.cpp"; then
    echo "Error: ${SRC_DIR} has v1 patch applied; reset to clean 28069d3 before applying $(basename "${PATCH_FILE}")" >&2
    exit 1
else
    echo "[1/3] Applying $(basename "${PATCH_FILE}")..."
    patch -d "${SRC_DIR}" -p1 < "${PATCH_FILE}"
fi

echo "[2/3] Building Release S0ixStandby.kext..."
make -C "${SRC_DIR}" clean
make -C "${SRC_DIR}" SDK="${SDK_DIR}"

OUT_KEXT="${SRC_DIR}/build/Release/S0ixStandby.kext"
echo "[3/3] Verifying Mach-O binary and Info.plist..."
file "${OUT_KEXT}/Contents/MacOS/S0ixStandby"
otool -hv "${OUT_KEXT}/Contents/MacOS/S0ixStandby"
plutil -lint "${OUT_KEXT}/Contents/Info.plist"
shasum -a 256 "${OUT_KEXT}/Contents/MacOS/S0ixStandby" "${OUT_KEXT}/Contents/Info.plist"
echo "Done: ${OUT_KEXT}"
