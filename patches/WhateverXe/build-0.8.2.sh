#!/usr/bin/env bash
# Reproducible WhateverXe 0.8.2 + X-STATION V2 patchset (wex-0.8.2-xstation-v1.patch)
# test and macOS Release build helper.
#
# Upstream pins (from WhateverXe 0.8.2 .github/workflows/ci.yml):
#   - b00t0x/WhateverXe       : tag 0.8.2 (4512191b9643e2391431fbd9639305b61568ee32)
#                               or main 2bad71718c6b4f9887e2a504a19a24c3593ea1db
#   - acidanthera/MacKernelSDK: 05094e5e88cec7caedbfb35e8449ed0db94bf95b
#   - acidanthera/Lilu        : tag 1.7.2
#
# Usage:
#   ./patches/WhateverXe/build-0.8.2.sh [--work-dir DIR] [--tests-only]
#
# On macOS (with Xcode Command Line Tools installed), produces:
#   <WORK_DIR>/WhateverXe/build/RELEASE/WhateverXe.kext
#   <WORK_DIR>/WhateverXe/build/RELEASE/AppleIntelTGLCompat.kext
#   <WORK_DIR>/WhateverXe/build/RELEASE/WhateverXe.kext.dSYM
#   <WORK_DIR>/WhateverXe/build/RELEASE/boot-stack.json
#   <WORK_DIR>/WhateverXe/build/RELEASE/build-manifest.json

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PATCH_FILE="${SCRIPT_DIR}/wex-0.8.2-xstation-v1.patch"

WEX_REPO="${WEX_REPO:-https://github.com/b00t0x/WhateverXe.git}"
WEX_REF="${WEX_REF:-0.8.2}"
WEX_EXPECTED_TAG_SHA="4512191b9643e2391431fbd9639305b61568ee32"
WEX_EXPECTED_MAIN_SHA="2bad71718c6b4f9887e2a504a19a24c3593ea1db"

MACKERNELSDK_REPO="${MACKERNELSDK_REPO:-https://github.com/acidanthera/MacKernelSDK.git}"
MACKERNELSDK_SHA="05094e5e88cec7caedbfb35e8449ed0db94bf95b"

LILU_REPO="${LILU_REPO:-https://github.com/acidanthera/Lilu.git}"
LILU_REF="1.7.2"

WORK_DIR="${REPO_ROOT}/third_party/wex-0.8.2-build"
TESTS_ONLY=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --tests-only)
      TESTS_ONLY=1
      shift
      ;;
    -h|--help)
      sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
  esac
done

if [[ ! -f "${PATCH_FILE}" ]]; then
  echo "ERROR: Patch file not found: ${PATCH_FILE}" >&2
  exit 1
fi

mkdir -p "${WORK_DIR}"
WORK_DIR="$(cd "${WORK_DIR}" && pwd)"
WEX_SRC="${WORK_DIR}/WhateverXe"

echo "==> [1/4] Preparing WhateverXe ${WEX_REF} in ${WEX_SRC}"
if [[ ! -d "${WEX_SRC}/.git" ]]; then
  git clone "${WEX_REPO}" "${WEX_SRC}"
fi
git -C "${WEX_SRC}" fetch --tags origin
git -C "${WEX_SRC}" reset --hard
git -C "${WEX_SRC}" clean -ffdx
git -C "${WEX_SRC}" checkout --detach "${WEX_REF}"

HEAD_SHA="$(git -C "${WEX_SRC}" rev-parse HEAD)"
if [[ "${HEAD_SHA}" != "${WEX_EXPECTED_TAG_SHA}" && "${HEAD_SHA}" != "${WEX_EXPECTED_MAIN_SHA}" ]]; then
  echo "WARNING: Checked-out SHA (${HEAD_SHA}) differs from pinned 0.8.2 (${WEX_EXPECTED_TAG_SHA} / ${WEX_EXPECTED_MAIN_SHA})" >&2
fi

echo "==> [2/4] Applying ${PATCH_FILE}"
git -C "${WEX_SRC}" apply --check "${PATCH_FILE}"
git -C "${WEX_SRC}" apply "${PATCH_FILE}"

echo "==> [3/4] Running WhateverXe 0.8.2 + X-STATION V2 host test suite"
rm -rf "${WEX_SRC}/build/tests"
if [[ "$(uname -s)" == "Darwin" ]]; then
  python3 "${WEX_SRC}/tools/test.py" --output "${WEX_SRC}/build/tests"
else
  echo "    (Non-Darwin host: running X-STATION V2 + cross-platform contract tests; full Darwin/Mach-O suite runs on macOS)"
  python3 "${WEX_SRC}/tools/test.py" --output "${WEX_SRC}/build/tests" \
    --only boot_arguments \
    --only boot_stack_limits \
    --only test_runner_contract \
    --only tgl_backlight_trace \
    --only tgl_bootstrap_aux \
    --only tgl_display_on_trace \
    --only tgl_display_timing_route \
    --only tgl_power_fuses \
    --only tgl_power_guard \
    --only tgl_sink_state_trace \
    --only tgl_timing_guard \
    --only tgl_wake_index
fi

if [[ "${TESTS_ONLY}" -eq 1 ]]; then
  echo "==> Host tests passed (--tests-only requested; skipping macOS kext build)."
  exit 0
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "==> Host OS is $(uname -s); skipping macOS xcrun kext link step."
  echo "    Re-run this script on macOS (with Xcode Command Line Tools) to build build/RELEASE/WhateverXe.kext."
  exit 0
fi

echo "==> [4/4] Fetching pinned MacKernelSDK (${MACKERNELSDK_SHA:0:8}) & Lilu (${LILU_REF}) and building Release kext"
SDK_DIR="${WORK_DIR}/MacKernelSDK"
LILU_SRC="${WORK_DIR}/Lilu"
LILU_KEXT="${WORK_DIR}/Lilu.kext"

if [[ ! -d "${SDK_DIR}/.git" ]]; then
  rm -rf "${SDK_DIR}"
  git clone "${MACKERNELSDK_REPO}" "${SDK_DIR}"
fi
git -C "${SDK_DIR}" fetch origin
git -C "${SDK_DIR}" reset --hard
git -C "${SDK_DIR}" clean -ffdx
git -C "${SDK_DIR}" checkout --detach "${MACKERNELSDK_SHA}"

if [[ ! -d "${LILU_SRC}/.git" ]]; then
  rm -rf "${LILU_SRC}"
  git clone "${LILU_REPO}" "${LILU_SRC}"
fi
git -C "${LILU_SRC}" fetch --tags origin
git -C "${LILU_SRC}" reset --hard
git -C "${LILU_SRC}" clean -ffdx
git -C "${LILU_SRC}" checkout --detach "${LILU_REF}"

rm -rf "${LILU_KEXT}"
mkdir -p "${LILU_KEXT}/Contents/Resources"
ln -sfn "${LILU_SRC}/Lilu/Headers" "${LILU_KEXT}/Contents/Resources/Headers"
ln -sfn "${LILU_SRC}/Lilu/Library" "${LILU_KEXT}/Contents/Resources/Library"

RELEASE_OUT="${WEX_SRC}/build/RELEASE"
rm -rf "${RELEASE_OUT}"
python3 "${WEX_SRC}/tools/build.py" \
  --release \
  --sdk "${SDK_DIR}" \
  --lilu "${LILU_KEXT}" \
  --output "${RELEASE_OUT}"

KEXT_BIN="${RELEASE_OUT}/WhateverXe.kext/Contents/MacOS/WhateverXe"
if [[ ! -f "${KEXT_BIN}" ]]; then
  echo "ERROR: Expected Release binary missing: ${KEXT_BIN}" >&2
  exit 1
fi

echo ""
echo "==> Build succeeded!"
echo "    WhateverXe.kext : ${RELEASE_OUT}/WhateverXe.kext"
echo "    Binary SHA-256  : $(shasum -a 256 "${KEXT_BIN}" | awk '{print $1}')"
echo "    Boot stack audit: ${RELEASE_OUT}/boot-stack.json"
echo "    Build manifest  : ${RELEASE_OUT}/build-manifest.json"
echo ""
echo "Next step (stage Phase 0 / Phase 1 USB test EFI without touching production EFI):"
echo "  python3 \"${REPO_ROOT}/tools/s0ix/stage_usb_efi.py\" --phase 0 --wex-kext \"${RELEASE_OUT}/WhateverXe.kext\" --out /tmp/xstation-usb-phase0"
