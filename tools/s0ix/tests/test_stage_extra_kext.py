#!/usr/bin/env python3
"""Tests for --extra-kext support in tools/s0ix/stage_usb_efi.py."""

from __future__ import annotations

import os
from pathlib import Path
import plistlib
import stat
import subprocess
import sys
import tempfile
import unittest

THIS_DIR = Path(__file__).resolve().parent
S0IX_DIR = THIS_DIR.parent
if str(S0IX_DIR) not in sys.path:
    sys.path.insert(0, str(S0IX_DIR))

import stage_usb_efi as mod  # noqa: E402


def _make_fake_kext(
    root: Path,
    bundle_name: str = "FocalTechSPI.kext",
    executable: str = "FocalTechSPI",
    bundle_id: str = "io.xstation.driver.FocalTechSPI",
    version: str = "0.1.0",
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
    (macos_dir / executable).write_bytes(b"\xcf\xfa\xed\xfekext-" + executable.encode("ascii"))
    return bundle


class StageExtraKextTests(unittest.TestCase):
    def setUp(self) -> None:
        self._td = tempfile.TemporaryDirectory(prefix="fspi_stage_extra_")
        self.tmp = Path(self._td.name)

    def tearDown(self) -> None:
        self._td.cleanup()

    def test_stage_usb_efi_installs_extra_kext_and_preserves_safety_guards(self) -> None:
        fake_kext = _make_fake_kext(self.tmp / "src_kext")
        out_dir = self.tmp / "usb_p4"

        res = mod.stage_usb_efi(
            phase=4,
            out_dir=out_dir,
            extra_kexts=[fake_kext],
        )
        self.assertIs(res["write_flash"], False)

        staged_bin = out_dir / "EFI" / "OC" / "Kexts" / "FocalTechSPI.kext" / "Contents" / "MacOS" / "FocalTechSPI"
        self.assertTrue(staged_bin.is_file())
        self.assertTrue(bool(staged_bin.stat().st_mode & stat.S_IXUSR))

        with (out_dir / "EFI" / "OC" / "config.plist").open("rb") as f:
            cfg = plistlib.load(f)

        self.assertIs(cfg["NVRAM"]["WriteFlash"], False)
        kernel_add = cfg["Kernel"]["Add"]
        bundle_paths = [k["BundlePath"] for k in kernel_add]
        self.assertIn("FocalTechSPI.kext", bundle_paths)
        if "VoodooI2C.kext" in bundle_paths:
            self.assertGreater(
                bundle_paths.index("FocalTechSPI.kext"),
                bundle_paths.index("VoodooI2C.kext"),
                "FocalTechSPI.kext must load after VoodooI2C.kext / VoodooGPIOTigerLakeLP",
            )

        entries = [k for k in kernel_add if k["BundlePath"] == "FocalTechSPI.kext"]
        self.assertEqual(len(entries), 1)
        entry = entries[0]
        self.assertIs(entry["Enabled"], True)
        self.assertEqual(entry["ExecutablePath"], "Contents/MacOS/FocalTechSPI")
        self.assertEqual(entry["PlistPath"], "Contents/Info.plist")
        self.assertEqual(entry["MinKernel"], "24.0.0")
        self.assertEqual(entry["Arch"], "x86_64")

        # Re-staging idempotency: no duplicate Kernel -> Add entry
        mod.stage_usb_efi(
            phase=4,
            out_dir=self.tmp / "usb_p4_again",
            efi_src=out_dir / "EFI",
            extra_kexts=[fake_kext],
        )
        with (self.tmp / "usb_p4_again" / "EFI" / "OC" / "config.plist").open("rb") as f:
            cfg2 = plistlib.load(f)
        self.assertEqual(
            len([k for k in cfg2["Kernel"]["Add"] if k["BundlePath"] == "FocalTechSPI.kext"]),
            1,
        )

        # Reject /Volumes/OPENCORE even when extra_kexts is passed
        with self.assertRaises(ValueError):
            mod.stage_usb_efi(
                phase=4,
                out_dir=Path("/Volumes/OPENCORE"),
                extra_kexts=[fake_kext],
            )

    def test_cli_extra_kext_flag(self) -> None:
        fake_kext = _make_fake_kext(self.tmp / "cli_src")
        out_dir = self.tmp / "cli_out"
        proc = subprocess.run(
            [
                sys.executable,
                "-B",
                str(S0IX_DIR / "stage_usb_efi.py"),
                "--phase",
                "4",
                "--out",
                str(out_dir),
                "--extra-kext",
                str(fake_kext),
            ],
            capture_output=True,
            text=True,
            env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"),
            check=False,
        )
        self.assertEqual(proc.returncode, 0, msg=f"CLI failed:\n{proc.stdout}\n{proc.stderr}")
        self.assertIn("FocalTechSPI.kext", proc.stdout)


if __name__ == "__main__":
    unittest.main()
