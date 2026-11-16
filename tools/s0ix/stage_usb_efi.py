#!/usr/bin/env python3
"""Deterministic USB-EFI staging tool for X-STATION V2 S0ix Phase 0, Phase 1 & Phase 4.

Enforces the Zero-Brick Safety Contract from docs/research/s0ix-roadmap-20261115.md:
  - Never modifies the repository's baseline EFI/ in place.
  - Forces NVRAM -> WriteFlash = False on staged USB config.plist so experimental
    boot-args remain RAM-only and never overwrite motherboard SPI NVRAM.
  - Widens 24.6.0-locked Kernel -> Add entries to 24.0.0 .. 24.99.99.
  - Phase 0: Keeps baseline production boot-args (SleepDisabled=1), excludes
    SSDT-DEEPIDLE.aml and S0ix DSDT patches / S0ixStandby.kext.
  - Phase 1: Applies the 2 binary-unique DSDT patches (_S3_ -> XS3_, PEPD._STA -> XSTA),
    enables SSDT-DEEPIDLE.aml, registers S0ixStandby.kext, retains all 5 Sharp 4K
    framebuffer guards (-wex-tgl-sdt-guard -noDC9 -nodisplaysleepDC6 -wex-no-panel-keep
    -wex-sink-rate-restore) alongside -wex-noaccel -s0ixdbg (since -wex-noaccel only
    disables AppleIntelTGLGraphics IntelAccelerator while AppleIntelTGLGraphicsFramebuffer
    still loads), and optionally binary-verifies all DSDT patches against a raw dsdt.dat dump.
  - Phase 4: Enables full Metal 3D + DC9 + dual Battery/AC S0ix standby by adding
    BAT0._BST -> XBST DSDT patch, SSDT-BATFULL.aml, patched S0ixStandby.kext (-s0ixacnoecwake),
    ScanPolicy = 524547, and Timeout = 3.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import stat
import sys
import tempfile
from typing import Any
import zipfile

APPLE_BOOT_GUID = "7C436110-AB2A-4BBB-A880-FE41995C9F82"
DSDT_SIGNATURE = b"DSDT"
XSTATION_DSDT_OEM_TABLE_ID = bytes.fromhex("41204D2049200000")  # "A M I \x00\x00" (QSBNIEkgAAA=)

PHASE0_BOOT_ARGS = (
    "-v keepsyms=1 debug=0x100 -allow3d -disablegfxfirmware "
    "alcid=19 alcapplid=19 -wex-tgl-sdt-guard -noDC9 -nodisplaysleepDC6 "
    "-wex-no-panel-keep -wex-sink-rate-restore"
)

PHASE1_BOOT_ARGS = (
    "-v keepsyms=1 debug=0x100 -allow3d -disablegfxfirmware "
    "alcid=19 alcapplid=19 -wex-tgl-sdt-guard -noDC9 -nodisplaysleepDC6 "
    "-wex-no-panel-keep -wex-sink-rate-restore -wex-noaccel -s0ixdbg"
)

PHASE4_BOOT_ARGS = (
    "-v keepsyms=1 debug=0x100 -allow3d -disablegfxfirmware "
    "alcid=19 alcapplid=19 -wex-tgl-sdt-guard -nodisplaysleepDC6 "
    "-wex-no-panel-keep -wex-sink-rate-restore -s0ixdbg "
    "-s0ixgpureset -s0ixrefclk -s0ixd3 -s0ixacnoecwake"
)

UNATTENDED_SCAN_POLICY = 524547  # OC_SCAN_FILE_SYSTEM_LOCK | OC_SCAN_DEVICE_LOCK | APFS | PCI NVMe
UNATTENDED_TIMEOUT = 3

S0IX_DSDT_PATCHES: tuple[dict[str, Any], ...] = (
    {
        "Base": "",
        "BaseSkip": 0,
        "Comment": "Disable _S3 for S0ix: Name(_S3_) to XS3_",
        "Count": 1,
        "Enabled": True,
        "Find": bytes.fromhex("085F53335F"),
        "Limit": 0,
        "Mask": b"",
        "OemTableId": XSTATION_DSDT_OEM_TABLE_ID,
        "Replace": bytes.fromhex("085853335F"),
        "ReplaceMask": b"",
        "Skip": 0,
        "TableLength": 0,
        "TableSignature": DSDT_SIGNATURE,
    },
    {
        "Base": "",
        "BaseSkip": 0,
        "Comment": "Expose PEPD under Darwin: PEPD._STA to XSTA",
        "Count": 1,
        "Enabled": True,
        "Find": bytes.fromhex("14275F53544100"),
        "Limit": 0,
        "Mask": b"",
        "OemTableId": XSTATION_DSDT_OEM_TABLE_ID,
        "Replace": bytes.fromhex("14275853544100"),
        "ReplaceMask": b"",
        "Skip": 0,
        "TableLength": 0,
        "TableSignature": DSDT_SIGNATURE,
    },
)

BATFULL_DSDT_PATCH: dict[str, Any] = {
    "Base": "",
    "BaseSkip": 0,
    "Comment": "Rename BAT0._BST to XBST for SSDT-BATFULL AC sleep fix",
    "Count": 1,
    "Enabled": True,
    "Find": bytes.fromhex("14490A5F4253540008504B4731"),
    "Limit": 0,
    "Mask": b"",
    "OemTableId": XSTATION_DSDT_OEM_TABLE_ID,
    "Replace": bytes.fromhex("14490A584253540008504B4731"),
    "ReplaceMask": b"",
    "Skip": 0,
    "TableLength": 0,
    "TableSignature": DSDT_SIGNATURE,
}

PHASE4_DSDT_PATCHES: tuple[dict[str, Any], ...] = S0IX_DSDT_PATCHES + (BATFULL_DSDT_PATCH,)

DEEPIDLE_ACPI_ENTRY: dict[str, Any] = {
    "Comment": "X-STATION V2 S0ix Deep Idle (LPS0/LXEN) + Darwin PEPD._STA + PWRB PRW0",
    "Enabled": True,
    "Path": "SSDT-DEEPIDLE.aml",
}

BATFULL_ACPI_ENTRY: dict[str, Any] = {
    "Comment": "X-STATION V2 Battery Full >=94% Threshold Fix for S0ix AC Standby",
    "Enabled": True,
    "Path": "SSDT-BATFULL.aml",
}

S0IX_KEXT_ENTRY: dict[str, Any] = {
    "Arch": "x86_64",
    "BundlePath": "S0ixStandby.kext",
    "Comment": "S0ixStandby v0.5.4 (PNP0D80 PEPD _DSM + LPIT/C-state telemetry + TGL GDRST)",
    "Enabled": True,
    "ExecutablePath": "Contents/MacOS/S0ixStandby",
    "MaxKernel": "",
    "MinKernel": "24.0.0",
    "PlistPath": "Contents/Info.plist",
}


def default_repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _is_subpath(candidate: Path, parent: Path) -> bool:
    try:
        candidate.relative_to(parent)
        return True
    except ValueError:
        return False


def validate_out_dir(out_dir: Path, efi_src: Path, repo_root: Path) -> Path:
    resolved_out = out_dir.resolve()
    resolved_src = efi_src.resolve()
    resolved_repo = repo_root.resolve()
    out_efi = (resolved_out / "EFI").resolve()

    if out_efi == resolved_src or _is_subpath(out_efi, resolved_src) or _is_subpath(resolved_src, out_efi):
        raise ValueError(f"Refusing to stage into or overlap source EFI directory: {resolved_src}")

    repo_efi = (resolved_repo / "EFI").resolve()
    if out_efi == repo_efi or _is_subpath(out_efi, repo_efi):
        raise ValueError(f"Refusing to overwrite repository baseline EFI directory: {repo_efi}")

    # Guard common internal production OpenCore mount point name unless user explicitly uses a USB dir.
    if resolved_out == Path("/Volumes/OPENCORE"):
        raise ValueError(
            "Refusing to stage directly into /Volumes/OPENCORE (production NVMe EFI safety guard)."
        )

    return out_efi


def verify_dsdt_binary(dsdt_path: Path, patches: list[dict[str, Any]]) -> dict[str, Any]:
    """Binary-verify ACPI DSDT header, OemTableId, and single-match (count == 1) patches."""
    if not dsdt_path.is_file():
        raise FileNotFoundError(f"DSDT binary not found: {dsdt_path}")

    raw = dsdt_path.read_bytes()
    if len(raw) < 36:
        raise ValueError(f"DSDT binary too short ({len(raw)} bytes < 36-byte ACPI header): {dsdt_path}")

    sig = raw[0:4]
    if sig != DSDT_SIGNATURE:
        raise ValueError(f"Expected ACPI signature b'DSDT', got {sig!r}")

    declared_len = int.from_bytes(raw[4:8], "little")
    if declared_len != len(raw):
        raise ValueError(f"ACPI header length {declared_len} != file size {len(raw)}")

    oem_id = raw[10:16]
    oem_table_id = raw[16:24]
    if oem_table_id != XSTATION_DSDT_OEM_TABLE_ID:
        raise ValueError(
            f"Unexpected DSDT OemTableId {oem_table_id!r} ({oem_table_id.hex().upper()}); "
            f"expected {XSTATION_DSDT_OEM_TABLE_ID!r} ({XSTATION_DSDT_OEM_TABLE_ID.hex().upper()})"
        )

    working = bytearray(raw)
    match_records: list[dict[str, Any]] = []

    for patch in patches:
        if not patch.get("Enabled", False):
            continue
        table_sig = patch.get("TableSignature", b"")
        if table_sig not in (b"", DSDT_SIGNATURE):
            continue

        patch_oem = patch.get("OemTableId", b"")
        if patch_oem not in (b"", b"\x00" * 8) and patch_oem != oem_table_id:
            raise ValueError(
                f"Patch {patch.get('Comment')!r} OemTableId {patch_oem!r} does not match DSDT {oem_table_id!r}"
            )

        find_bytes: bytes = patch["Find"]
        replace_bytes: bytes = patch["Replace"]
        if not find_bytes or len(find_bytes) != len(replace_bytes):
            raise ValueError(
                f"Patch {patch.get('Comment')!r} has invalid Find/Replace lengths "
                f"({len(find_bytes)} vs {len(replace_bytes)})"
            )
        if patch.get("Mask", b"") or patch.get("ReplaceMask", b""):
            raise ValueError(f"Masked DSDT patches are not supported by verifier: {patch.get('Comment')!r}")

        expected_count = patch.get("Count", 0)
        raw_count = raw.count(find_bytes)
        work_count = working.count(find_bytes)
        if raw_count != 1 or work_count != 1:
            raise ValueError(
                f"Patch {patch.get('Comment')!r} expected unique single match (1), "
                f"got raw_count={raw_count}, sequential_count={work_count}"
            )
        if expected_count not in (0, 1):
            raise ValueError(f"Patch {patch.get('Comment')!r} has unexpected Count={expected_count}")

        offset = bytes(working).find(find_bytes)
        working[offset : offset + len(find_bytes)] = replace_bytes
        if working.count(find_bytes) != 0:
            raise ValueError(f"Patch {patch.get('Comment')!r} left residual Find matches after replacement")

        match_records.append(
            {
                "comment": patch.get("Comment", ""),
                "offset": offset,
                "count": raw_count,
                "find_hex": find_bytes.hex().upper(),
                "replace_hex": replace_bytes.hex().upper(),
            }
        )

    return {
        "path": str(dsdt_path),
        "size": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "oem_id": oem_id.decode("ascii", errors="replace").strip(),
        "oem_table_id_hex": oem_table_id.hex().upper(),
        "matches": match_records,
    }


def _resolve_kext_source(src: Path, bundle_name: str, temp_Holder: list[tempfile.TemporaryDirectory[str]]) -> Path:
    if src.is_dir():
        if src.name == bundle_name:
            return src
        candidate = src / bundle_name
        if candidate.is_dir():
            return candidate
        raise ValueError(f"Directory {src} is not {bundle_name} and does not contain {bundle_name}")

    if src.is_file() and src.suffix.lower() == ".zip":
        td = tempfile.TemporaryDirectory(prefix="s0ix_kext_")
        temp_Holder.append(td)
        extract_root = Path(td.name)
        with zipfile.ZipFile(src, "r") as zf:
            zf.extractall(extract_root)
        for p in extract_root.rglob(bundle_name):
            if p.is_dir() and "__MACOSX" not in p.parts:
                return p
        raise ValueError(f"Archive {src} does not contain {bundle_name}")

    raise FileNotFoundError(f"Kext source not found or unsupported format: {src}")


def install_kext_bundle(
    src: Path,
    dest_kexts_dir: Path,
    bundle_name: str,
    expected_executable: str,
    expected_identifier: str | None = None,
) -> dict[str, str]:
    """Validate and copy a .kext bundle (from directory or .zip) into EFI/OC/Kexts/."""
    temp_dirs: list[tempfile.TemporaryDirectory[str]] = []
    try:
        bundle_dir = _resolve_kext_source(src, bundle_name, temp_dirs)
        info_plist = bundle_dir / "Contents" / "Info.plist"
        exec_file = bundle_dir / "Contents" / "MacOS" / expected_executable

        if not info_plist.is_file():
            raise ValueError(f"Missing Contents/Info.plist in {bundle_dir}")
        if not exec_file.is_file() or exec_file.stat().st_size == 0:
            raise ValueError(f"Missing or empty Contents/MacOS/{expected_executable} in {bundle_dir}")

        with info_plist.open("rb") as f:
            info = plistlib.load(f)

        bundle_exec = info.get("CFBundleExecutable")
        if bundle_exec != expected_executable:
            raise ValueError(
                f"{bundle_name} Info.plist CFBundleExecutable={bundle_exec!r}, expected {expected_executable!r}"
            )

        bundle_id = info.get("CFBundleIdentifier", "")
        if expected_identifier and bundle_id != expected_identifier:
            raise ValueError(
                f"{bundle_name} Info.plist CFBundleIdentifier={bundle_id!r}, expected {expected_identifier!r}"
            )

        dest_bundle = dest_kexts_dir / bundle_name
        if dest_bundle.exists():
            shutil.rmtree(dest_bundle)

        shutil.copytree(
            bundle_dir,
            dest_bundle,
            ignore=shutil.ignore_patterns(".DS_Store", "__MACOSX"),
        )
        dest_exec = dest_bundle / "Contents" / "MacOS" / expected_executable
        dest_exec.chmod(dest_exec.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

        return {
            "bundle_name": bundle_name,
            "bundle_id": str(bundle_id),
            "version": str(info.get("CFBundleShortVersionString") or info.get("CFBundleVersion") or ""),
            "exec_sha256": sha256_file(dest_exec),
            "plist_sha256": sha256_file(dest_bundle / "Contents" / "Info.plist"),
        }
    finally:
        for td in temp_dirs:
            td.cleanup()


def transform_config_plist(
    config: dict[str, Any],
    phase: int,
    wex_version: str | None = None,
    extra_boot_args: str | None = None,
) -> dict[str, Any]:
    """Pure transformation of OpenCore config.plist dict for Phase 0, Phase 1, or Phase 4."""
    if phase not in (0, 1, 4):
        raise ValueError(f"Unsupported phase: {phase} (expected 0, 1, or 4)")

    cfg = copy.deepcopy(config)

    # 1. Zero-Brick Safety Contract: Force NVRAM -> WriteFlash = False on all USB test EFIs.
    nvram = cfg.setdefault("NVRAM", {})
    nvram["WriteFlash"] = False

    nvram_delete = nvram.setdefault("Delete", {})
    boot_delete = nvram_delete.setdefault(APPLE_BOOT_GUID, [])
    if "boot-args" not in boot_delete:
        boot_delete.append("boot-args")

    # 2. Widen 24.6.0-locked Kernel -> Add entries to 24.0.0 .. 24.99.99 across Sequoia 15.x.
    kernel_add: list[dict[str, Any]] = cfg.setdefault("Kernel", {}).setdefault("Add", [])
    for entry in kernel_add:
        if entry.get("MinKernel") == "24.6.0":
            entry["MinKernel"] = "24.0.0"
        if entry.get("MaxKernel") == "24.6.0":
            entry["MaxKernel"] = "24.99.99"
        if wex_version and entry.get("BundlePath") == "WhateverXe.kext":
            entry["Comment"] = f"WhateverXe {wex_version} + X-STATION V2 patchset (staged USB)"

    acpi = cfg.setdefault("ACPI", {})
    acpi_add: list[dict[str, Any]] = acpi.setdefault("Add", [])
    acpi_patch: list[dict[str, Any]] = acpi.setdefault("Patch", [])
    all_s0ix_patch_keys = {(p["Find"], p["Replace"], p["TableSignature"]) for p in PHASE4_DSDT_PATCHES}
    batfull_key = (
        BATFULL_DSDT_PATCH["Find"],
        BATFULL_DSDT_PATCH["Replace"],
        BATFULL_DSDT_PATCH["TableSignature"],
    )

    nvram_add_boot = nvram.setdefault("Add", {}).setdefault(APPLE_BOOT_GUID, {})

    if phase == 0:
        # Phase 0: Restore pre-S0ix (-noDC9 / SleepDisabled=1) boot-args, exclude SSDT-DEEPIDLE.aml,
        # SSDT-BATFULL.aml, S0ix DSDT patches, and S0ixStandby.kext.
        acpi["Add"] = [
            e for e in acpi_add if e.get("Path") not in (DEEPIDLE_ACPI_ENTRY["Path"], BATFULL_ACPI_ENTRY["Path"])
        ]
        acpi["Patch"] = [
            p
            for p in acpi_patch
            if (p.get("Find"), p.get("Replace"), p.get("TableSignature")) not in all_s0ix_patch_keys
        ]
        cfg["Kernel"]["Add"] = [k for k in kernel_add if k.get("BundlePath") != S0IX_KEXT_ENTRY["BundlePath"]]
        boot_args = PHASE0_BOOT_ARGS
    else:
        if phase == 1:
            acpi_add[:] = [e for e in acpi_add if e.get("Path") != BATFULL_ACPI_ENTRY["Path"]]
            acpi_patch[:] = [
                p
                for p in acpi_patch
                if (p.get("Find"), p.get("Replace"), p.get("TableSignature")) != batfull_key
            ]
            target_patches = S0IX_DSDT_PATCHES
            target_acpi_entries = (DEEPIDLE_ACPI_ENTRY,)
            boot_args = PHASE1_BOOT_ARGS
        else:
            target_patches = PHASE4_DSDT_PATCHES
            target_acpi_entries = (DEEPIDLE_ACPI_ENTRY, BATFULL_ACPI_ENTRY)
            boot_args = PHASE4_BOOT_ARGS
            misc = cfg.setdefault("Misc", {})
            misc.setdefault("Security", {})["ScanPolicy"] = UNATTENDED_SCAN_POLICY
            boot_sec = misc.setdefault("Boot", {})
            boot_sec["Timeout"] = UNATTENDED_TIMEOUT
            boot_sec["ShowPicker"] = True

        for target_patch in target_patches:
            key = (target_patch["Find"], target_patch["Replace"], target_patch["TableSignature"])
            existing = next(
                (
                    p
                    for p in acpi_patch
                    if (p.get("Find"), p.get("Replace"), p.get("TableSignature")) == key
                ),
                None,
            )
            if existing is not None:
                existing.clear()
                existing.update(copy.deepcopy(target_patch))
            else:
                acpi_patch.append(copy.deepcopy(target_patch))

        for target_entry in target_acpi_entries:
            existing_acpi = next(
                (e for e in acpi_add if e.get("Path") == target_entry["Path"]),
                None,
            )
            if existing_acpi is not None:
                existing_acpi.clear()
                existing_acpi.update(copy.deepcopy(target_entry))
            else:
                acpi_add.append(copy.deepcopy(target_entry))

        existing_s0ix_kext = next(
            (k for k in kernel_add if k.get("BundlePath") == S0IX_KEXT_ENTRY["BundlePath"]),
            None,
        )
        if existing_s0ix_kext is not None:
            existing_s0ix_kext.clear()
            existing_s0ix_kext.update(copy.deepcopy(S0IX_KEXT_ENTRY))
        else:
            kernel_add.append(copy.deepcopy(S0IX_KEXT_ENTRY))

    if extra_boot_args:
        extra = extra_boot_args.strip()
        if extra:
            existing_tokens = boot_args.split()
            for tok in extra.split():
                if tok not in existing_tokens:
                    existing_tokens.append(tok)
            boot_args = " ".join(existing_tokens)

    nvram_add_boot["boot-args"] = boot_args
    return cfg


def stage_usb_efi(
    *,
    phase: int,
    out_dir: Path,
    efi_src: Path | None = None,
    repo_root: Path | None = None,
    wex_kext: Path | None = None,
    s0ix_kext: Path | None = None,
    verify_dsdt: Path | None = None,
    extra_boot_args: str | None = None,
    force: bool = False,
) -> dict[str, Any]:
    """Copy EFI/ into <out_dir>/EFI and deterministically stage Phase 0, Phase 1, or Phase 4 config/assets."""
    if phase not in (0, 1, 4):
        raise ValueError(f"Unsupported phase: {phase} (expected 0, 1, or 4)")
    if phase == 0 and s0ix_kext is not None:
        raise ValueError("--s0ix-kext is only valid with --phase 1 or --phase 4 (Phase 0 keeps SleepDisabled=1)")

    root = (repo_root or default_repo_root()).resolve()
    src_efi = (efi_src or (root / "EFI")).resolve()
    src_config = src_efi / "OC" / "config.plist"
    if not src_config.is_file():
        raise FileNotFoundError(f"Source OpenCore config.plist not found: {src_config}")

    out_efi = validate_out_dir(out_dir, src_efi, root)
    if out_efi.exists():
        if not force:
            raise FileExistsError(
                f"Target {out_efi} already exists. Back up your stable USB EFI first or pass --force."
            )
        shutil.rmtree(out_efi)

    out_efi.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src_efi, out_efi, ignore=shutil.ignore_patterns(".DS_Store", "__MACOSX"))

    staged_acpi_dir = out_efi / "OC" / "ACPI"
    staged_kexts_dir = out_efi / "OC" / "Kexts"
    staged_acpi_dir.mkdir(parents=True, exist_ok=True)
    staged_kexts_dir.mkdir(parents=True, exist_ok=True)

    deepidle_aml = staged_acpi_dir / DEEPIDLE_ACPI_ENTRY["Path"]
    batfull_aml = staged_acpi_dir / BATFULL_ACPI_ENTRY["Path"]
    if phase == 0:
        for p_aml in (deepidle_aml, batfull_aml):
            if p_aml.exists():
                p_aml.unlink()
        staged_s0ix_dir = staged_kexts_dir / S0IX_KEXT_ENTRY["BundlePath"]
        if staged_s0ix_dir.exists():
            shutil.rmtree(staged_s0ix_dir)
    else:
        if not deepidle_aml.is_file():
            repo_deepidle = root / "EFI" / "OC" / "ACPI" / DEEPIDLE_ACPI_ENTRY["Path"]
            if repo_deepidle.is_file():
                shutil.copy2(repo_deepidle, deepidle_aml)
            else:
                raise FileNotFoundError(
                    f"Phase {phase} requires {DEEPIDLE_ACPI_ENTRY['Path']} in {staged_acpi_dir}"
                )
        if phase == 1:
            if batfull_aml.exists():
                batfull_aml.unlink()
        elif phase == 4:
            if not batfull_aml.is_file():
                repo_batfull = root / "EFI" / "OC" / "ACPI" / BATFULL_ACPI_ENTRY["Path"]
                if repo_batfull.is_file():
                    shutil.copy2(repo_batfull, batfull_aml)
                else:
                    raise FileNotFoundError(
                        f"Phase 4 requires {BATFULL_ACPI_ENTRY['Path']} in {staged_acpi_dir}"
                    )

    wex_info: dict[str, str] | None = None
    if wex_kext is not None:
        wex_info = install_kext_bundle(
            wex_kext,
            staged_kexts_dir,
            bundle_name="WhateverXe.kext",
            expected_executable="WhateverXe",
        )

    s0ix_info: dict[str, str] | None = None
    if s0ix_kext is not None:
        s0ix_info = install_kext_bundle(
            s0ix_kext,
            staged_kexts_dir,
            bundle_name="S0ixStandby.kext",
            expected_executable="S0ixStandby",
            expected_identifier="com.labrouss.S0ixStandby",
        )

    with src_config.open("rb") as f:
        base_cfg = plistlib.load(f)

    staged_cfg = transform_config_plist(
        base_cfg,
        phase=phase,
        wex_version=wex_info["version"] if wex_info else None,
        extra_boot_args=extra_boot_args,
    )

    dsdt_report: dict[str, Any] | None = None
    if verify_dsdt is not None:
        patches_to_check = list(staged_cfg["ACPI"]["Patch"])
        if phase == 0:
            patches_to_check.extend(copy.deepcopy(list(PHASE4_DSDT_PATCHES)))
        dsdt_report = verify_dsdt_binary(verify_dsdt, patches_to_check)

    staged_config_path = out_efi / "OC" / "config.plist"
    plist_bytes = plistlib.dumps(staged_cfg, fmt=plistlib.FMT_XML, sort_keys=True)
    staged_config_path.write_bytes(plist_bytes)

    warnings: list[str] = []
    if not (out_efi / "OC" / "OpenCore.efi").is_file():
        warnings.append(
            "OpenCore.efi not present in staged EFI (run `bash scripts/fetch-thirdparty.sh` before booting a physical USB)."
        )
    if phase in (1, 4) and not (staged_kexts_dir / "S0ixStandby.kext" / "Contents" / "MacOS" / "S0ixStandby").is_file():
        warnings.append(
            f"S0ixStandby.kext binary is not yet copied into EFI/OC/Kexts/ (pass `--s0ix-kext <path>` before booting Phase {phase})."
        )

    return {
        "phase": phase,
        "out_efi": str(out_efi),
        "config_plist": str(staged_config_path),
        "config_sha256": hashlib.sha256(plist_bytes).hexdigest(),
        "write_flash": staged_cfg["NVRAM"]["WriteFlash"],
        "boot_args": staged_cfg["NVRAM"]["Add"][APPLE_BOOT_GUID]["boot-args"],
        "wex_kext": wex_info,
        "s0ix_kext": s0ix_info,
        "dsdt_verification": dsdt_report,
        "warnings": warnings,
    }


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Deterministically stage X-STATION V2 S0ix Phase 0 / Phase 1 / Phase 4 OpenCore USB EFI."
    )
    parser.add_argument(
        "--phase",
        type=int,
        choices=(0, 1, 4),
        required=True,
        help="Target roadmap phase: 0 (baseline SleepDisabled=1), 1 (Gate 1 Deep Idle + -wex-noaccel), or 4 (Metal 3D + DC9 + AC sleep SSDT-BATFULL + -s0ixacnoecwake).",
    )
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="Output directory (creates <out>/EFI; never modifies repository EFI/ in place).",
    )
    parser.add_argument(
        "--efi-src",
        type=Path,
        default=None,
        help="Optional source EFI directory (defaults to <repo>/EFI).",
    )
    parser.add_argument(
        "--wex-kext",
        type=Path,
        default=None,
        help="Optional path to custom-built WhateverXe.kext directory or .zip to install into staged EFI/OC/Kexts/.",
    )
    parser.add_argument(
        "--s0ix-kext",
        type=Path,
        default=None,
        help="Optional path to S0ixStandby.kext directory or Release .zip (Phase 1 and Phase 4).",
    )
    parser.add_argument(
        "--verify-dsdt",
        type=Path,
        default=None,
        help="Optional path to raw firmware dsdt.dat to binary-verify OemTableId and single-match (count==1) DSDT patches.",
    )
    parser.add_argument(
        "--extra-boot-args",
        type=str,
        default=None,
        help="Optional extra boot-args tokens to append (e.g. '-s0ixgpureset').",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite <out>/EFI if it already exists.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    try:
        summary = stage_usb_efi(
            phase=args.phase,
            out_dir=args.out,
            efi_src=args.efi_src,
            wex_kext=args.wex_kext,
            s0ix_kext=args.s0ix_kext,
            verify_dsdt=args.verify_dsdt,
            extra_boot_args=args.extra_boot_args,
            force=args.force,
        )
    except Exception as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1

    print(f"[OK] Staged Phase {summary['phase']} USB EFI -> {summary['out_efi']}")
    print(f"     config.plist SHA256 : {summary['config_sha256']}")
    print(f"     NVRAM -> WriteFlash : {summary['write_flash']}")
    print(f"     boot-args           : {summary['boot_args']}")
    if summary["wex_kext"]:
        w = summary["wex_kext"]
        print(f"     WhateverXe.kext     : v{w['version']} (MacOS SHA256: {w['exec_sha256'][:16]}...)")
    if summary["s0ix_kext"]:
        s = summary["s0ix_kext"]
        print(f"     S0ixStandby.kext    : v{s['version']} (MacOS SHA256: {s['exec_sha256'][:16]}...)")
    if summary["dsdt_verification"]:
        d = summary["dsdt_verification"]
        print(
            f"     DSDT binary check   : PASSED ({d['size']} B, OemTableId={d['oem_table_id_hex']}, "
            f"{len(d['matches'])} patches verified)"
        )
        for m in d["matches"]:
            print(f"       - 0x{m['offset']:05X} (count={m['count']}): {m['comment']}")
    for warn in summary["warnings"]:
        print(f"[NOTE] {warn}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
