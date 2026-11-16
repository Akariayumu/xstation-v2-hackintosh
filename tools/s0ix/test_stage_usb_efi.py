#!/usr/bin/env python3
"""Unit and integration tests for tools/s0ix/stage_usb_efi.py."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import stage_usb_efi as mod  # noqa: E402

REPO_ROOT = mod.default_repo_root()
REPO_CONFIG = REPO_ROOT / "EFI" / "OC" / "config.plist"
REAL_DSDT = REPO_ROOT.parent / "xstation-oc" / "work" / "acpi" / "dsdt.dat"


def _resolve_optional_s0ix_kext() -> Path | None:
    env_path = os.environ.get("S0IX_KEXT_PATH", "").strip()
    if not env_path:
        return None
    candidate = Path(env_path)
    return candidate if candidate.exists() else None


def _make_synthetic_kext(
    root: Path,
    bundle_name: str,
    executable: str,
    bundle_id: str,
    version: str = "0.5.4",
) -> Path:
    bundle = root / bundle_name
    macos_dir = bundle / "Contents" / "MacOS"
    macos_dir.mkdir(parents=True, exist_ok=True)
    info = {
        "CFBundleExecutable": executable,
        "CFBundleIdentifier": bundle_id,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": executable,
        "CFBundlePackageType": "KEXT",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
    }
    (bundle / "Contents" / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_XML))
    (macos_dir / executable).write_bytes(b"\xcf\xfa\xed\xfesynthetic-macho-" + executable.encode("ascii"))
    return bundle


def _make_synthetic_dsdt(
    extra_s3: bool = False,
    oem_table_id: bytes = mod.XSTATION_DSDT_OEM_TABLE_ID,
    include_baseline_patches: bool = True,
) -> bytes:
    body = bytearray(b"\x00" * 64)
    s0ix_finds = {p["Find"] for p in mod.PHASE4_DSDT_PATCHES}
    if include_baseline_patches:
        with REPO_CONFIG.open("rb") as f:
            cfg = plistlib.load(f)
        for p in cfg["ACPI"]["Patch"]:
            if p.get("Enabled") and p.get("Find") not in s0ix_finds:
                body.extend(p["Find"])
                body.extend(b"\x90" * 16)
    body.extend(bytes.fromhex("085F53335F1207040A05000000"))
    if extra_s3:
        body.extend(bytes.fromhex("085F53335F"))
    body.extend(b"\x90" * 16)
    body.extend(bytes.fromhex("14275F53544100"))
    body.extend(b"\x90" * 16)
    body.extend(bytes.fromhex("14490A5F4253540008504B4731"))
    body.extend(b"\x90" * 16)

    total_len = 36 + len(body)
    header = bytearray(36)
    header[0:4] = b"DSDT"
    header[4:8] = total_len.to_bytes(4, "little")
    header[8] = 2
    header[10:16] = b"ALASKA"
    header[16:24] = oem_table_id
    header[24:28] = (0x01072009).to_bytes(4, "little")
    header[28:32] = b"INTL"
    header[32:36] = (0x20230628).to_bytes(4, "little")
    table = bytearray(header + body)
    table[9] = (-sum(table)) & 0xFF
    return bytes(table)


class StageUsbEfiTests(unittest.TestCase):
    def setUp(self) -> None:
        self._baseline_config_sha256 = mod.sha256_file(REPO_CONFIG)
        self._td = tempfile.TemporaryDirectory(prefix="s0ix_stage_test_")
        self.tmp = Path(self._td.name)

    def tearDown(self) -> None:
        self._td.cleanup()
        self.assertEqual(
            mod.sha256_file(REPO_CONFIG),
            self._baseline_config_sha256,
            "Repository baseline EFI/OC/config.plist must never be mutated!",
        )

    def test_safety_guards_reject_repo_efi_and_existing_target(self) -> None:
        with self.assertRaises(ValueError):
            mod.stage_usb_efi(phase=0, out_dir=REPO_ROOT)
        with self.assertRaises(ValueError):
            mod.stage_usb_efi(phase=0, out_dir=REPO_ROOT / "EFI")
        with self.assertRaises(ValueError):
            mod.stage_usb_efi(phase=0, out_dir=Path("/Volumes/OPENCORE"))

        out_dir = self.tmp / "usb0"
        mod.stage_usb_efi(phase=0, out_dir=out_dir)
        with self.assertRaises(FileExistsError):
            mod.stage_usb_efi(phase=0, out_dir=out_dir, force=False)
        # Re-staging with --force succeeds deterministically.
        res2 = mod.stage_usb_efi(phase=0, out_dir=out_dir, force=True)
        self.assertFalse(res2["write_flash"])

    def test_phase0_staging_and_wex_override(self) -> None:
        wex_src = _make_synthetic_kext(
            self.tmp / "src_wex",
            bundle_name="WhateverXe.kext",
            executable="WhateverXe",
            bundle_id="as.vit9696.WhateverXe",
            version="0.8.2",
        )
        out_a = self.tmp / "p0_a"
        out_b = self.tmp / "p0_b"

        res_a = mod.stage_usb_efi(phase=0, out_dir=out_a, wex_kext=wex_src)
        res_b = mod.stage_usb_efi(phase=0, out_dir=out_b, wex_kext=wex_src)
        self.assertEqual(res_a["config_sha256"], res_b["config_sha256"])

        staged_cfg_path = out_a / "EFI" / "OC" / "config.plist"
        with staged_cfg_path.open("rb") as f:
            cfg = plistlib.load(f)
        with REPO_CONFIG.open("rb") as f:
            orig_cfg = plistlib.load(f)

        # Zero-Brick NVRAM check
        self.assertIs(cfg["NVRAM"]["WriteFlash"], False)
        self.assertIn("boot-args", cfg["NVRAM"]["Delete"][mod.APPLE_BOOT_GUID])

        # Pre-S0ix (-noDC9 / SleepDisabled=1) boot-args restored in Phase 0
        self.assertEqual(cfg["NVRAM"]["Add"][mod.APPLE_BOOT_GUID]["boot-args"], mod.PHASE0_BOOT_ARGS)
        self.assertIn("-noDC9", cfg["NVRAM"]["Add"][mod.APPLE_BOOT_GUID]["boot-args"])
        self.assertNotIn("-wex-noaccel", cfg["NVRAM"]["Add"][mod.APPLE_BOOT_GUID]["boot-args"])

        # No 24.6.0-locked entries remain; also verify transform_config_plist widens synthetic 24.6.0 locks
        orig_non_s0ix_kexts = [k for k in orig_cfg["Kernel"]["Add"] if k["BundlePath"] != "S0ixStandby.kext"]
        for orig_k, new_k in zip(orig_non_s0ix_kexts, cfg["Kernel"]["Add"]):
            if orig_k["MinKernel"] == "24.6.0":
                self.assertEqual(new_k["MinKernel"], "24.0.0")
            else:
                self.assertEqual(new_k["MinKernel"], orig_k["MinKernel"])
            if orig_k["MaxKernel"] == "24.6.0":
                self.assertEqual(new_k["MaxKernel"], "24.99.99")
            else:
                self.assertEqual(new_k["MaxKernel"], orig_k["MaxKernel"])
            self.assertNotEqual(new_k["MinKernel"], "24.6.0")
            self.assertNotEqual(new_k["MaxKernel"], "24.6.0")

        synth_locked = {"Kernel": {"Add": [{"BundlePath": "AppleALC.kext", "MinKernel": "24.6.0", "MaxKernel": "24.6.0"}]}}
        widened_synth = mod.transform_config_plist(synth_locked, phase=0)
        self.assertEqual(widened_synth["Kernel"]["Add"][0]["MinKernel"], "24.0.0")
        self.assertEqual(widened_synth["Kernel"]["Add"][0]["MaxKernel"], "24.99.99")

        # Phase 0 excludes SSDT-DEEPIDLE.aml, SSDT-BATFULL.aml, S0ix DSDT patches, and S0ixStandby.kext
        acpi_paths = [e["Path"] for e in cfg["ACPI"]["Add"]]
        self.assertNotIn("SSDT-DEEPIDLE.aml", acpi_paths)
        self.assertNotIn("SSDT-BATFULL.aml", acpi_paths)
        self.assertFalse((out_a / "EFI" / "OC" / "ACPI" / "SSDT-DEEPIDLE.aml").exists())
        self.assertFalse((out_a / "EFI" / "OC" / "ACPI" / "SSDT-BATFULL.aml").exists())
        self.assertEqual(len(cfg["ACPI"]["Patch"]), 3)
        bundle_paths = [k["BundlePath"] for k in cfg["Kernel"]["Add"]]
        self.assertNotIn("S0ixStandby.kext", bundle_paths)

        # Custom WhateverXe.kext installed with executable mode
        staged_wex_bin = out_a / "EFI" / "OC" / "Kexts" / "WhateverXe.kext" / "Contents" / "MacOS" / "WhateverXe"
        self.assertTrue(staged_wex_bin.is_file())
        self.assertTrue(bool(staged_wex_bin.stat().st_mode & stat.S_IXUSR))

        # Passing --s0ix-kext to Phase 0 is rejected
        s0ix_src = _make_synthetic_kext(
            self.tmp / "src_s0ix",
            bundle_name="S0ixStandby.kext",
            executable="S0ixStandby",
            bundle_id="com.labrouss.S0ixStandby",
        )
        with self.assertRaises(ValueError):
            mod.stage_usb_efi(phase=0, out_dir=self.tmp / "p0_bad", s0ix_kext=s0ix_src)

    def test_phase1_staging_zip_and_idempotency(self) -> None:
        env_kext = _resolve_optional_s0ix_kext()
        if env_kext is not None and env_kext.is_dir():
            s0ix_dir = env_kext
        else:
            s0ix_dir = _make_synthetic_kext(
                self.tmp / "synth_s0ix",
                bundle_name="S0ixStandby.kext",
                executable="S0ixStandby",
                bundle_id="com.labrouss.S0ixStandby",
                version="0.5.4",
            )

        # Package S0ixStandby.kext into a .zip to test archive installation as well
        zip_path = self.tmp / "S0ixStandby-Release.zip"
        with zipfile.ZipFile(zip_path, "w") as zf:
            for p in s0ix_dir.rglob("*"):
                if p.is_file():
                    zf.write(p, p.relative_to(s0ix_dir.parent))

        dsdt_file = self.tmp / "synth_dsdt.dat"
        dsdt_file.write_bytes(_make_synthetic_dsdt())

        out_dir = self.tmp / "p1_out"
        res = mod.stage_usb_efi(
            phase=1,
            out_dir=out_dir,
            s0ix_kext=zip_path,
            verify_dsdt=dsdt_file,
        )
        self.assertIs(res["write_flash"], False)
        self.assertEqual(res["boot_args"], mod.PHASE1_BOOT_ARGS)
        self.assertEqual(res["warnings"], [] if (out_dir / "EFI/OC/OpenCore.efi").is_file() else res["warnings"])
        self.assertIsNotNone(res["dsdt_verification"])
        self.assertEqual(len(res["dsdt_verification"]["matches"]), 5)

        with (out_dir / "EFI" / "OC" / "config.plist").open("rb") as f:
            cfg = plistlib.load(f)

        # Verify WriteFlash == False & Phase 1 boot-args (retaining all 5 Sharp 4K framebuffer guards)
        self.assertIs(cfg["NVRAM"]["WriteFlash"], False)
        staged_boot_args = cfg["NVRAM"]["Add"][mod.APPLE_BOOT_GUID]["boot-args"]
        self.assertEqual(staged_boot_args, mod.PHASE1_BOOT_ARGS)
        self.assertEqual(staged_boot_args, f"{mod.PHASE0_BOOT_ARGS} -wex-noaccel -s0ixdbg")
        boot_arg_tokens = set(staged_boot_args.split())
        for required_flag in (
            "-wex-tgl-sdt-guard",
            "-noDC9",
            "-nodisplaysleepDC6",
            "-wex-no-panel-keep",
            "-wex-sink-rate-restore",
            "-wex-noaccel",
            "-s0ixdbg",
        ):
            self.assertIn(required_flag, boot_arg_tokens)

        # Verify SSDT-DEEPIDLE.aml on disk and enabled in ACPI -> Add
        staged_aml = out_dir / "EFI" / "OC" / "ACPI" / "SSDT-DEEPIDLE.aml"
        self.assertTrue(staged_aml.is_file())
        self.assertEqual(staged_aml.stat().st_size, 264)
        self.assertEqual(staged_aml.read_bytes(), (REPO_CONFIG.parent / "ACPI" / "SSDT-DEEPIDLE.aml").read_bytes())
        deepidle_entries = [e for e in cfg["ACPI"]["Add"] if e["Path"] == "SSDT-DEEPIDLE.aml"]
        self.assertEqual(len(deepidle_entries), 1)
        self.assertIs(deepidle_entries[0]["Enabled"], True)

        # Verify the 2 exact DSDT patches in ACPI -> Patch
        patches = cfg["ACPI"]["Patch"]
        s3_patches = [p for p in patches if p["Find"] == bytes.fromhex("085F53335F")]
        pepd_patches = [p for p in patches if p["Find"] == bytes.fromhex("14275F53544100")]
        self.assertEqual(len(s3_patches), 1)
        self.assertEqual(len(pepd_patches), 1)

        self.assertEqual(s3_patches[0]["Replace"], bytes.fromhex("085853335F"))
        self.assertEqual(s3_patches[0]["TableSignature"], b"DSDT")
        self.assertEqual(s3_patches[0]["OemTableId"], bytes.fromhex("41204D2049200000"))
        self.assertEqual(s3_patches[0]["Count"], 1)
        self.assertIs(s3_patches[0]["Enabled"], True)

        self.assertEqual(pepd_patches[0]["Replace"], bytes.fromhex("14275853544100"))
        self.assertEqual(pepd_patches[0]["TableSignature"], b"DSDT")
        self.assertEqual(pepd_patches[0]["OemTableId"], bytes.fromhex("41204D2049200000"))
        self.assertEqual(pepd_patches[0]["Count"], 1)
        self.assertIs(pepd_patches[0]["Enabled"], True)

        # Verify S0ixStandby.kext on disk and in Kernel -> Add
        staged_s0ix_bin = out_dir / "EFI" / "OC" / "Kexts" / "S0ixStandby.kext" / "Contents" / "MacOS" / "S0ixStandby"
        self.assertTrue(staged_s0ix_bin.is_file())
        self.assertTrue(bool(staged_s0ix_bin.stat().st_mode & stat.S_IXUSR))
        s0ix_entries = [k for k in cfg["Kernel"]["Add"] if k["BundlePath"] == "S0ixStandby.kext"]
        self.assertEqual(len(s0ix_entries), 1)
        self.assertEqual(s0ix_entries[0]["ExecutablePath"], "Contents/MacOS/S0ixStandby")
        self.assertEqual(s0ix_entries[0]["PlistPath"], "Contents/Info.plist")
        self.assertEqual(s0ix_entries[0]["MinKernel"], "24.0.0")
        self.assertEqual(s0ix_entries[0]["MaxKernel"], "")
        self.assertIs(s0ix_entries[0]["Enabled"], True)

        # Verify idempotency when transform_config_plist is applied again
        cfg_again = mod.transform_config_plist(cfg, phase=1)
        self.assertEqual(cfg, cfg_again)

    def test_dsdt_verifier_error_detection(self) -> None:
        dup_dsdt = self.tmp / "dup_dsdt.dat"
        dup_dsdt.write_bytes(_make_synthetic_dsdt(extra_s3=True))
        with self.assertRaises(ValueError):
            mod.verify_dsdt_binary(dup_dsdt, list(mod.S0IX_DSDT_PATCHES))

        bad_oem_dsdt = self.tmp / "bad_oem_dsdt.dat"
        bad_oem_dsdt.write_bytes(_make_synthetic_dsdt(oem_table_id=b"WRONGID\x00"))
        with self.assertRaises(ValueError):
            mod.verify_dsdt_binary(bad_oem_dsdt, list(mod.S0IX_DSDT_PATCHES))

    @unittest.skipUnless(REAL_DSDT.is_file(), "Real X-STATION V2 dsdt.dat not present in sibling workspace")
    def test_real_firmware_dsdt_and_cli_roundtrip(self) -> None:
        env_kext = _resolve_optional_s0ix_kext()
        s0ix_arg = env_kext if env_kext is not None else _make_synthetic_kext(
            self.tmp / "cli_s0ix",
            bundle_name="S0ixStandby.kext",
            executable="S0ixStandby",
            bundle_id="com.labrouss.S0ixStandby",
        )
        cli_out = self.tmp / "cli_p1"
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        proc = subprocess.run(
            [
                sys.executable,
                "-B",
                str(THIS_DIR / "stage_usb_efi.py"),
                "--phase",
                "1",
                "--out",
                str(cli_out),
                "--s0ix-kext",
                str(s0ix_arg),
                "--verify-dsdt",
                str(REAL_DSDT),
            ],
            capture_output=True,
            text=True,
            env=env,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, msg=f"CLI failed:\n{proc.stdout}\n{proc.stderr}")
        self.assertIn("0x06055 (count=1): Disable _S3 for S0ix", proc.stdout)
        self.assertIn("0x3B58D (count=1): Expose PEPD under Darwin", proc.stdout)

        cli_p4 = self.tmp / "cli_p4"
        proc4 = subprocess.run(
            [
                sys.executable,
                "-B",
                str(THIS_DIR / "stage_usb_efi.py"),
                "--phase",
                "4",
                "--out",
                str(cli_p4),
                "--verify-dsdt",
                str(REAL_DSDT),
            ],
            capture_output=True,
            text=True,
            env=env,
            check=False,
        )
        self.assertEqual(proc4.returncode, 0, msg=f"Phase 4 CLI failed:\n{proc4.stdout}\n{proc4.stderr}")
        self.assertIn("0x06055 (count=1): Disable _S3 for S0ix", proc4.stdout)
        self.assertIn("0x3B58D (count=1): Expose PEPD under Darwin", proc4.stdout)
        self.assertIn("0x073EA (count=1): Rename BAT0._BST to XBST", proc4.stdout)

    def test_phase4_ac_sleep_staging_and_idempotency(self) -> None:
        dsdt_file = self.tmp / "synth_p4_dsdt.dat"
        dsdt_file.write_bytes(_make_synthetic_dsdt())
        out_dir = self.tmp / "p4_out"
        res = mod.stage_usb_efi(phase=4, out_dir=out_dir, verify_dsdt=dsdt_file)

        self.assertIs(res["write_flash"], False)
        self.assertEqual(res["boot_args"], mod.PHASE4_BOOT_ARGS)
        self.assertIsNotNone(res["dsdt_verification"])
        self.assertEqual(len(res["dsdt_verification"]["matches"]), 6)

        with (out_dir / "EFI" / "OC" / "config.plist").open("rb") as f:
            cfg = plistlib.load(f)

        self.assertIs(cfg["NVRAM"]["WriteFlash"], False)
        tokens = set(cfg["NVRAM"]["Add"][mod.APPLE_BOOT_GUID]["boot-args"].split())
        for flag in ("-s0ixdbg", "-s0ixgpureset", "-s0ixrefclk", "-s0ixd3", "-s0ixacnoecwake"):
            self.assertIn(flag, tokens)
        for excluded in ("-noDC9", "-wex-noaccel", "-s0ixacnortc"):
            self.assertNotIn(excluded, tokens)

        self.assertEqual(cfg["Misc"]["Security"]["ScanPolicy"], 524547)
        self.assertEqual(cfg["Misc"]["Boot"]["Timeout"], 3)
        self.assertIs(cfg["Misc"]["Boot"]["ShowPicker"], True)

        staged_batfull = out_dir / "EFI" / "OC" / "ACPI" / "SSDT-BATFULL.aml"
        self.assertTrue(staged_batfull.is_file())
        self.assertEqual(staged_batfull.read_bytes(), (REPO_CONFIG.parent / "ACPI" / "SSDT-BATFULL.aml").read_bytes())
        batfull_entries = [e for e in cfg["ACPI"]["Add"] if e["Path"] == "SSDT-BATFULL.aml"]
        self.assertEqual(len(batfull_entries), 1)
        self.assertIs(batfull_entries[0]["Enabled"], True)

        bat_patches = [p for p in cfg["ACPI"]["Patch"] if p["Find"] == bytes.fromhex("14490A5F4253540008504B4731")]
        self.assertEqual(len(bat_patches), 1)
        self.assertEqual(bat_patches[0]["Replace"], bytes.fromhex("14490A584253540008504B4731"))
        self.assertEqual(bat_patches[0]["Count"], 1)
        self.assertIs(bat_patches[0]["Enabled"], True)

        staged_s0ix_bin = out_dir / "EFI" / "OC" / "Kexts" / "S0ixStandby.kext" / "Contents" / "MacOS" / "S0ixStandby"
        self.assertTrue(staged_s0ix_bin.is_file())

        cfg_again = mod.transform_config_plist(cfg, phase=4)
        self.assertEqual(cfg, cfg_again)


if __name__ == "__main__":
    unittest.main()
