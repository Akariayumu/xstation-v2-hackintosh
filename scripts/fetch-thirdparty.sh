#!/usr/bin/env bash
# 从各上游发布页下载第三方组件，按 thirdparty.lock 取出文件放进 EFI，并按 EFI-sha256.txt 校验整个 EFI。
# 结果与作者实机使用的 v0.1.55 逐字节一致（config.plist 除外，它的 SMBIOS 需要你自己填）。
# 依赖：bash、curl、unzip、sha256sum 或 shasum。Linux 和 macOS 都可以运行。
# 用法：scripts/fetch-thirdparty.sh            下载缓存在 .cache/thirdparty/
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

CACHE=.cache/thirdparty
mkdir -p "$CACHE"

if command -v sha256sum >/dev/null; then
    sha256() { sha256sum "$1" | cut -d' ' -f1; }
else
    sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
fi

echo "==> 下载并校验发布包"
while IFS=$'\t' read -r kind name digest url; do
    [[ "$kind" == "archive" ]] || continue
    f="$CACHE/$name"
    if [[ ! -f "$f" ]] || [[ "$(sha256 "$f")" != "$digest" ]]; then
        echo "    $name"
        curl -fsSL --retry 3 -o "$f.part" "$url"
        mv "$f.part" "$f"
    fi
    if [[ "$(sha256 "$f")" != "$digest" ]]; then
        echo "!!  $name 的 SHA-256 不符，上游文件可能被替换。已停止。" >&2
        exit 1
    fi
done < thirdparty.lock

echo "==> 取出文件"
n=0
while IFS=$'\t' read -r kind dest archive member digest; do
    [[ "$kind" == "file" ]] || continue
    mkdir -p "$(dirname -- "$dest")"
    if [[ "$member" == "-" ]]; then
        cp "$CACHE/$archive" "$dest"
    else
        unzip -p "$CACHE/$archive" "$member" > "$dest"
    fi
    [[ "$dest" == */MacOS/* ]] && chmod +x "$dest"
    n=$((n + 1))
done < thirdparty.lock
echo "    $n 个文件"

echo "==> 校验 EFI"
bad=0
while read -r digest path; do
    if [[ ! -f "$path" ]]; then
        echo "!!  缺少 $path" >&2; bad=1
    elif [[ "$(sha256 "$path")" != "$digest" ]]; then
        echo "!!  不一致 $path" >&2; bad=1
    fi
done < EFI-sha256.txt
if [[ $bad -ne 0 ]]; then
    echo "EFI 校验失败。" >&2
    exit 1
fi
echo "    $(wc -l < EFI-sha256.txt | tr -d ' ') 个文件全部一致"
echo
echo "完成。下一步：在 EFI/OC/config.plist 的 PlatformInfo → Generic 填入你自己生成的 SMBIOS（见 README）。"
