#!/usr/bin/env python3
"""Install vendored libmicroros prebuilt into managed_components (CE01).

Skips the long libmicroros.mk build on Windows. Run after:
  idf.py add-dependency "micro-ros/micro_ros_espidf_component>=22.0.0,<23.0.0"
  (or scripts/deep_dog/deep_dog_fetch_microros.sh)

Then on Windows: python scripts/deep_dog/patch_microros_windows_cmake.py
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREBUILT_ROOT = ROOT / "tools" / "microros_prebuilt"
DEFAULT_PROFILE = "esp32s3-idf5.5-humble22"
COMP_REL = Path("managed_components/micro-ros__micro_ros_espidf_component")
ARTIFACTS = ("libmicroros.a", "include", "include_override")


def sha256_file(path: Path) -> str:
    """Hash a text file after normalizing line endings (CRLF/CR -> LF).

    The prebuilt MANIFEST records the LF-form sha256 produced on Mac/Linux;
    Windows checkouts with core.autocrlf=true expand to CRLF, so raw-hash
    would mismatch for an otherwise identical file.
    """
    raw = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return hashlib.sha256(raw).hexdigest()


def load_manifest(profile_dir: Path) -> dict:
    manifest_path = profile_dir / "MANIFEST.json"
    if not manifest_path.is_file():
        raise SystemExit(f"missing {manifest_path}")
    return json.loads(manifest_path.read_text(encoding="utf-8"))


def verify_manifest(manifest: dict, profile_dir: Path) -> None:
    meta = ROOT / "app-colcon.meta"
    if not meta.is_file():
        raise SystemExit(f"missing {meta}")
    expected = manifest.get("app_colcon_meta_sha256", "")
    actual = sha256_file(meta)
    if expected and actual != expected:
        raise SystemExit(
            f"app-colcon.meta mismatch:\n  expected {expected}\n  actual   {actual}\n"
            "Rebuild prebuilt on Mac/Linux or update MANIFEST after intentional meta change."
        )
    for name in ARTIFACTS:
        p = profile_dir / name
        if not p.exists():
            raise SystemExit(f"prebuilt artifact missing: {p}")


def copy_tree(src: Path, dst: Path) -> None:
    if dst.is_dir():
        shutil.rmtree(dst)
    elif dst.exists():
        dst.unlink()
    if src.is_dir():
        shutil.copytree(src, dst)
    else:
        shutil.copy2(src, dst)


def install(profile: str, force: bool) -> None:
    profile_dir = PREBUILT_ROOT / profile
    if not profile_dir.is_dir():
        raise SystemExit(f"unknown profile {profile!r} (no {profile_dir})")

    manifest = load_manifest(profile_dir)
    verify_manifest(manifest, profile_dir)

    comp = ROOT / COMP_REL
    if not comp.is_dir():
        raise SystemExit(
            f"missing {comp}\n"
            "Run first: ./scripts/deep_dog/deep_dog_fetch_microros.sh\n"
            "  or: idf.py add-dependency \"micro-ros/micro_ros_espidf_component>=22.0.0,<23.0.0\""
        )

    lib_dst = comp / "libmicroros.a"
    if lib_dst.is_file() and not force:
        print(f"[install_microros_prebuilt] {lib_dst} already exists (use --force to overwrite)")

    for name in ARTIFACTS:
        src = profile_dir / name
        dst = comp / name
        print(f"[install_microros_prebuilt] {src.relative_to(ROOT)} -> {dst.relative_to(ROOT)}")
        copy_tree(src, dst)

    print(f"[install_microros_prebuilt] OK profile={profile} idf={manifest.get('idf_version')}")
    print("Next:")
    print("  Windows: python scripts/deep_dog/patch_microros_windows_cmake.py")
    print("  All:     idf.py set-target esp32s3 && idf.py -DDEEP_DOG_MICROROS=ON build")
    print("  Expect:  [deep-dog] using existing libmicroros.a (no WSL libmicroros build)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--profile",
        default=DEFAULT_PROFILE,
        help=f"prebuilt profile under tools/microros_prebuilt/ (default {DEFAULT_PROFILE})",
    )
    ap.add_argument("--force", action="store_true", help="overwrite existing libmicroros.a")
    ap.add_argument("--list", action="store_true", help="list available profiles")
    args = ap.parse_args()

    if args.list:
        if not PREBUILT_ROOT.is_dir():
            print("(no tools/microros_prebuilt/)")
            return 0
        for d in sorted(PREBUILT_ROOT.iterdir()):
            if d.is_dir() and (d / "MANIFEST.json").is_file():
                m = load_manifest(d)
                print(f"{d.name}\t idf={m.get('idf_version')} microros={m.get('microros_component', {}).get('version')}")
        return 0

    install(args.profile, args.force)
    return 0


if __name__ == "__main__":
    sys.exit(main())
