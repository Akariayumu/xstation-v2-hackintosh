#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
ROOT=$(pwd -P)
SDK_HEADERS=${SDK_HEADERS:-./MacKernelSDK/Headers}
KMOD_LIB=${KMOD_LIB:-"${SDK_HEADERS%/Headers}/Library/x86_64/libkmod.a"}
CLANG=${CLANG:-clang-18}
LOCAL_LD64=${LD64:-ld64}
if [[ -n "${LD64:-}" ]]; then
    :
elif [[ -x "$LOCAL_LD64" ]]; then
    LD64=$LOCAL_LD64
else
    LD64=ld64.lld-18
fi
KEXT_NM=${KEXT_NM:-llvm-nm-18}
KEXT_OBJDUMP=${KEXT_OBJDUMP:-llvm-objdump-18}
MODE=${1:-}
if [[ "$MODE" != "" && "$MODE" != "--objects-only" ]] || (( $# > 1 )); then
    echo "Usage: ./build.sh [--objects-only]" >&2
    exit 2
fi
for tool in "$CLANG" "$LD64" "$KEXT_NM" "$KEXT_OBJDUMP" python3; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
test -f "$SDK_HEADERS/IOKit/IOService.h"
test -f "$KMOD_LIB"
# Fixed output path; never follow an output-directory symlink outside this tree.
python3 - "$ROOT" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
for name in ('build', 'build/XSHIDDLog.kext', 'build/XSHIDDLog.kext/Contents',
             'build/XSHIDDLog.kext/Contents/MacOS'):
    p = root / name
    if p.is_symlink():
        raise SystemExit(f'Refusing output symlink: {p}')
    p.mkdir(exist_ok=True)
for p in (root / 'build').rglob('*'):
    if p.is_symlink():
        raise SystemExit(f'Refusing output symlink: {p}')
# Invalidate any previous bundle executable before starting a new build.
(root / 'build/XSHIDDLog.kext/Contents/MacOS/XSHIDDLog').unlink(missing_ok=True)
PY
OUT="$ROOT/build"
BUNDLE="$OUT/XSHIDDLog.kext/Contents"
COMMON=(-target x86_64-apple-macosx10.15 -isystem "$SDK_HEADERS"
    -DKERNEL -DKERNEL_EXTENSION -mkernel -fno-builtin
    -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
    -fno-common -fno-unwind-tables -fno-asynchronous-unwind-tables
    -O2 -Wall -Wextra -Werror)
"$CLANG" "${COMMON[@]}" -std=c++14 -fapple-kext -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -fno-use-cxa-atexit -fno-register-global-dtors-with-atexit \
    -mllvm -disable-atexit-based-global-dtor-lowering \
    -c XSHIDDLog.cpp -o "$OUT/XSHIDDLog.o"
# Standard kmod metadata glue; _start/_stop are supplied by SDK libkmod.a.
cat > "$OUT/kmod_info.c" <<'C'
#include <mach/kmod.h>
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(org.xstation.driver.XSHIDDLog, "0.1.0", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
C
"$CLANG" "${COMMON[@]}" -c "$OUT/kmod_info.c" -o "$OUT/kmod_info.o"
"$KEXT_NM" -u "$OUT/XSHIDDLog.o" > "$OUT/object-imports.txt"
python3 - "$OUT/object-imports.txt" <<'PY'
import pathlib, sys
symbols = set(pathlib.Path(sys.argv[1]).read_text().split())
bad = symbols & {'___cxa_atexit', '_atexit', '___dso_handle', 'dyld_stub_binder', '_main'}
if bad:
    raise SystemExit(f'ERROR: unexpected userspace imports in driver object: {sorted(bad)}')
PY
if [[ "$MODE" == "--objects-only" ]]; then
    echo "Compiled x86_64 Mach-O objects: build/XSHIDDLog.o, build/kmod_info.o"
    echo "Objects only; no loadable kext produced."
    exit 0
fi
# cctools ld64 (253.x) predates -platform_version; ld64.lld uses it.
if [[ -n "${LD_TARGET_FLAGS+x}" ]]; then
    read -r -a TARGET_FLAGS <<<"$LD_TARGET_FLAGS"
elif [[ "$(basename -- "$LD64")" == *ld64.lld* ]]; then
    TARGET_FLAGS=(-platform_version macos 10.15 10.15)
else
    TARGET_FLAGS=(-macosx_version_min 10.15 -sdk_version 10.15)
fi
"$LD64" -arch x86_64 -kext "${TARGET_FLAGS[@]}" \
    -undefined dynamic_lookup -no_uuid \
    "$OUT/XSHIDDLog.o" "$OUT/kmod_info.o" "$KMOD_LIB" \
    -o "$OUT/XSHIDDLog.link-check.macho"
"$KEXT_NM" -u "$OUT/XSHIDDLog.link-check.macho" > "$OUT/undefined-symbols.txt"
"$KEXT_OBJDUMP" --macho --private-header "$OUT/XSHIDDLog.link-check.macho" > "$OUT/macho-header.txt"
python3 - "$OUT" <<'PY'
import pathlib, re, sys
out = pathlib.Path(sys.argv[1])
if not re.search(r'\bKEXT_?BUNDLE\b', (out / 'macho-header.txt').read_text()):
    raise SystemExit('ERROR: linker did not produce MH_KEXT_BUNDLE. '
                     'ld64.lld-18 ignores -kext. Do not inject the diagnostic binary. '
                     'Use a kext-capable Apple/cctools ld64 via LD64, '
                     'or ./build.sh --objects-only for compile validation.')
symbols = set((out / 'undefined-symbols.txt').read_text().split())
bad = symbols & {'__realmain', '__antimain', '__start', '__stop', '_kmod_info',
                 '_main', 'dyld_stub_binder', '___cxa_atexit'}
if bad:
    raise SystemExit(f'ERROR: unexpected unresolved runtime/glue symbols: {sorted(bad)}')
PY
cp -- "$OUT/XSHIDDLog.link-check.macho" "$BUNDLE/MacOS/XSHIDDLog"
python3 - "$ROOT/Info.plist" "$BUNDLE/Info.plist" <<'PY'
import pathlib, plistlib, sys
source, dest = map(pathlib.Path, sys.argv[1:])
data = plistlib.loads(source.read_bytes())
assert data['CFBundleExecutable'] == 'XSHIDDLog'
assert data['IOKitPersonalities']['XSHIDDLog']['IONameMatch'] == 'INTC1051'
dest.write_bytes(plistlib.dumps(data, sort_keys=False))
PY
echo 'Built x86_64 MH_KEXT_BUNDLE: build/XSHIDDLog.kext'
echo 'Kernel imports: build/undefined-symbols.txt (not a target-system link validation)'
