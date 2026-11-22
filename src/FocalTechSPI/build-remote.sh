#!/usr/bin/env bash
# Validate FocalTechSPI pure tests + Linux -mkernel object compile, then build
# MH_KEXT_BUNDLE on target Mac via .testmac.env and copy back to build/Release/FocalTechSPI.kext.
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LOCAL_SDK="${MAC_KERNEL_SDK:-${REPO_ROOT}/.cache/S0ixStandby-src/MacKernelSDK}"
ENV_FILE="${TESTMAC_ENV:-${REPO_ROOT}/../Hackintosh_app/xsensei/.testmac.env}"

echo "==> [1/3] Running host FocalTechSPIPure unit tests"
LOCAL_CHECK="${SCRIPT_DIR}/build/local-check"
mkdir -p "${LOCAL_CHECK}"
clang++-18 -std=c++14 -Wall -Wextra -Werror "${SCRIPT_DIR}/tests/test_focaltech_spi_pure.cpp" -o "${LOCAL_CHECK}/test_fspi_pure"
"${LOCAL_CHECK}/test_fspi_pure"

echo "==> [2/3] Running Linux clang-18 -mkernel --objects-only compile & symbol check"
if [[ ! -d "${LOCAL_SDK}/Headers" || ! -f "${LOCAL_SDK}/Library/x86_64/libkmod.a" ]]; then
    echo "Error: MacKernelSDK not found at ${LOCAL_SDK}" >&2
    exit 1
fi

COMMON_KFLAGS=(
    -target x86_64-apple-macosx10.15
    -isystem "${LOCAL_SDK}/Headers"
    -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
    -mkernel -fno-builtin -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
    -fno-common -fno-unwind-tables -fno-asynchronous-unwind-tables
    -O2 -Wall -Wextra -Werror -Wno-unused-parameter
    -std=c++14 -fapple-kext -fno-exceptions -fno-rtti
    -fno-threadsafe-statics -fno-use-cxa-atexit -fno-register-global-dtors-with-atexit
    -mllvm -disable-atexit-based-global-dtor-lowering
)

clang++-18 "${COMMON_KFLAGS[@]}" -c "${SCRIPT_DIR}/FocalTechSPI.cpp" -o "${LOCAL_CHECK}/FocalTechSPI.o"
clang++-18 "${COMMON_KFLAGS[@]}" -c "${SCRIPT_DIR}/FocalTechSPIUserClient.cpp" -o "${LOCAL_CHECK}/FocalTechSPIUserClient.o"
llvm-nm-18 -u "${LOCAL_CHECK}/FocalTechSPI.o" "${LOCAL_CHECK}/FocalTechSPIUserClient.o" > "${LOCAL_CHECK}/object-imports.txt"

python3 - "${LOCAL_CHECK}/object-imports.txt" <<'PY'
import pathlib, sys
symbols = set(pathlib.Path(sys.argv[1]).read_text().split())
bad = symbols & {'___cxa_atexit', '_atexit', '___dso_handle', 'dyld_stub_binder', '_main'}
if bad:
    raise SystemExit(f'ERROR: forbidden userspace imports in kext objects: {sorted(bad)}')
PY

if [[ "${1:-}" == "--local-only" ]]; then
    echo "Local compile & symbol checks passed (--local-only)."
    exit 0
fi

echo "==> [3/3] Linking & verifying MH_KEXT_BUNDLE on target Mac"
if [[ ! -f "${ENV_FILE}" ]]; then
    echo "Error: .testmac.env not found at ${ENV_FILE}" >&2
    exit 1
fi

set -a
# shellcheck disable=SC1090
. "${ENV_FILE}"
set +a
export SSHPASS="${MAC_PASSWORD}"

TARGET_HOST="${MAC_HOST}"
PING_HOST="${TARGET_HOST#*@}"
PING_HOST="${PING_HOST#[}"
PING_HOST="${PING_HOST%]}"
SSH_BASE_OPTS=(-o StrictHostKeyChecking=accept-new)
SSH_OPTS=("${SSH_BASE_OPTS[@]}" -o ConnectTimeout=10)
RSYNC_EXTRA=()

if [[ "${PING_HOST}" == *:* ]]; then
    ping -6 -c 1 -W 1 "${PING_HOST}" >/dev/null 2>&1 || true
    SSH_OPTS=(-6 "${SSH_BASE_OPTS[@]}" -o ConnectTimeout=10)
    RSYNC_EXTRA=(--ipv6)
else
    ping -c 1 -W 1 -I wlp4s0 "${PING_HOST}" >/dev/null 2>&1 || true
    WLP_IPV4=$(ip -4 -o addr show dev wlp4s0 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -n1 || true)
    PROBE_OPTS=("${SSH_BASE_OPTS[@]}" -o ConnectTimeout=3)
    if [[ -n "${WLP_IPV4}" ]]; then
        PROBE_OPTS+=(-o "BindAddress=${WLP_IPV4}")
    fi
    if sshpass -e ssh "${PROBE_OPTS[@]}" "${TARGET_HOST}" true >/dev/null 2>&1; then
        if [[ -n "${WLP_IPV4}" ]]; then
            SSH_OPTS+=(-o "BindAddress=${WLP_IPV4}")
        fi
    elif [[ -n "${MAC_IPV6_HOST:-}" ]]; then
        TARGET_HOST="${MAC_IPV6_HOST}"
        SSH_OPTS=(-6 "${SSH_BASE_OPTS[@]}" -o ConnectTimeout=10)
        RSYNC_EXTRA=(--ipv6)
    fi
fi

REMOTE_BUILD_DIR="/tmp/FocalTechSPI-build"
if [[ "${TARGET_HOST#*@}" == *:* ]]; then
    RSYNC_USER="${TARGET_HOST%@*}"
    RSYNC_ADDR="${TARGET_HOST#*@}"
    RSYNC_ADDR="${RSYNC_ADDR#[}"
    RSYNC_ADDR="${RSYNC_ADDR%]}"
    RSYNC_HOST="${RSYNC_USER}@[${RSYNC_ADDR}]"
else
    RSYNC_HOST="${TARGET_HOST}"
fi

sshpass -e ssh "${SSH_OPTS[@]}" "${TARGET_HOST}" "mkdir -p ${REMOTE_BUILD_DIR}/MacKernelSDK"
rsync -az "${RSYNC_EXTRA[@]}" --delete -e "sshpass -e ssh ${SSH_OPTS[*]}" \
    --exclude build --exclude .DS_Store \
    "${SCRIPT_DIR}/" "${RSYNC_HOST}:${REMOTE_BUILD_DIR}/"
rsync -az "${RSYNC_EXTRA[@]}" -e "sshpass -e ssh ${SSH_OPTS[*]}" \
    --exclude .git \
    "${LOCAL_SDK}/" "${RSYNC_HOST}:${REMOTE_BUILD_DIR}/MacKernelSDK/"

sshpass -e ssh "${SSH_OPTS[@]}" "${TARGET_HOST}" "set -euo pipefail
cd ${REMOTE_BUILD_DIR}
make clean
make SDK=${REMOTE_BUILD_DIR}/MacKernelSDK
BIN=build/Release/FocalTechSPI.kext/Contents/MacOS/FocalTechSPI
PLIST=build/Release/FocalTechSPI.kext/Contents/Info.plist
file \"\${BIN}\"
otool -hv \"\${BIN}\" | tee build/macho-hv.txt
grep -Eq 'KEXT_?BUNDLE' build/macho-hv.txt
nm -u \"\${BIN}\" > build/undef.txt
python3 - build/undef.txt <<'PY'
import pathlib, sys
syms = set(pathlib.Path(sys.argv[1]).read_text().split())
bad = syms & {'__realmain', '__antimain', '__start', '__stop', '_kmod_info', '_main', 'dyld_stub_binder', '___cxa_atexit'}
if bad:
    raise SystemExit(f'Forbidden unresolved symbols: {sorted(bad)}')
PY
plutil -lint \"\${PLIST}\"
shasum -a 256 \"\${BIN}\" \"\${PLIST}\"
"

mkdir -p "${SCRIPT_DIR}/build/Release"
rm -rf "${SCRIPT_DIR}/build/Release/FocalTechSPI.kext"
rsync -az "${RSYNC_EXTRA[@]}" -e "sshpass -e ssh ${SSH_OPTS[*]}" \
    "${RSYNC_HOST}:${REMOTE_BUILD_DIR}/build/Release/FocalTechSPI.kext" \
    "${SCRIPT_DIR}/build/Release/"

echo "==> Ready: ${SCRIPT_DIR}/build/Release/FocalTechSPI.kext"
