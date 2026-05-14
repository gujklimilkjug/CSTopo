from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path
from typing import Any, Dict, Optional


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPO_ROOT / "Config" / "CSTopoPdalRuntime.json"


class BootstrapError(RuntimeError):
    pass


def _load_manifest(path: Path) -> Dict[str, Any]:
    if not path.exists():
        raise BootstrapError(f"PDAL runtime manifest was not found: {path}")
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise BootstrapError(f"PDAL runtime manifest is not valid JSON: {path}: {exc}") from exc

    required = ("version", "download_url", "sha256", "install_dir", "executable_path")
    missing = [name for name in required if not str(manifest.get(name, "")).strip()]
    if missing:
        raise BootstrapError(f"PDAL runtime manifest is missing required value(s): {', '.join(missing)}")

    checksum = str(manifest["sha256"]).strip().lower()
    if len(checksum) != 64 or any(char not in "0123456789abcdef" for char in checksum):
        raise BootstrapError(
            "PDAL runtime manifest has no usable SHA-256 checksum. "
            "Package the runtime, publish the ZIP, and update Config/CSTopoPdalRuntime.json first."
        )

    return manifest


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _download_file(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(url) as response, destination.open("wb") as handle:
            shutil.copyfileobj(response, handle)
    except Exception as exc:
        raise BootstrapError(f"Failed to download PDAL runtime from {url}: {exc}") from exc


def _verify_checksum(path: Path, expected_sha256: str) -> None:
    actual = _sha256_file(path)
    if actual.lower() != expected_sha256.lower():
        raise BootstrapError(
            f"PDAL runtime checksum mismatch for {path}.\n"
            f"Expected: {expected_sha256.lower()}\n"
            f"Actual:   {actual.lower()}"
        )


def _safe_extract(zip_path: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    destination_root = destination.resolve()
    try:
        with zipfile.ZipFile(zip_path) as archive:
            for member in archive.infolist():
                target = (destination / member.filename).resolve()
                try:
                    target.relative_to(destination_root)
                except ValueError as exc:
                    raise BootstrapError(f"Refusing to extract unsafe ZIP member: {member.filename}") from exc
            archive.extractall(destination)
    except zipfile.BadZipFile as exc:
        raise BootstrapError(f"PDAL runtime ZIP is not a valid ZIP file: {zip_path}") from exc


def _run_pdal_version(pdal_path: Path) -> str:
    env = os.environ.copy()
    bin_dir = str(pdal_path.parent)
    env["PATH"] = bin_dir + os.pathsep + env.get("PATH", "")
    runtime_root = pdal_path.parent.parent
    data_dirs = {
        "PDAL_DRIVER_PATH": runtime_root / "apps" / "pdal" / "plugins",
        "GDAL_DATA": runtime_root / "apps" / "gdal" / "share" / "gdal",
        "GDAL_DRIVER_PATH": runtime_root / "apps" / "gdal" / "lib" / "gdalplugins",
        "PROJ_DATA": runtime_root / "share" / "proj",
    }
    for name, path in data_dirs.items():
        if path.exists():
            env[name] = str(path)

    try:
        result = subprocess.run(
            [str(pdal_path), "--version"],
            cwd=str(pdal_path.parent),
            env=env,
            check=True,
            capture_output=True,
            text=True,
        )
    except Exception as exc:
        raise BootstrapError(f"Installed PDAL runtime did not pass 'pdal.exe --version': {exc}") from exc

    return (result.stdout or result.stderr).strip()


def bootstrap_pdal_runtime(
    manifest_path: Path = DEFAULT_MANIFEST,
    runtime_zip: Optional[Path] = None,
    *,
    repo_root: Path = REPO_ROOT,
    skip_exec_validation: bool = False,
) -> Path:
    manifest = _load_manifest(manifest_path)
    install_dir = (repo_root / str(manifest["install_dir"])).resolve()
    executable_path = install_dir / str(manifest["executable_path"])
    expected_version = str(manifest["version"]).strip()
    expected_sha256 = str(manifest["sha256"]).strip().lower()

    if runtime_zip is None:
        with tempfile.TemporaryDirectory(prefix="cstopo-pdal-") as temp_dir:
            zip_path = Path(temp_dir) / Path(str(manifest["download_url"])).name
            print(f"Downloading PDAL runtime {expected_version}...")
            _download_file(str(manifest["download_url"]), zip_path)
            _verify_checksum(zip_path, expected_sha256)
            _safe_extract(zip_path, install_dir)
    else:
        zip_path = runtime_zip.resolve()
        if not zip_path.exists():
            raise BootstrapError(f"Local PDAL runtime ZIP was not found: {zip_path}")
        _verify_checksum(zip_path, expected_sha256)
        _safe_extract(zip_path, install_dir)

    if not executable_path.exists():
        raise BootstrapError(f"PDAL runtime extracted, but pdal.exe was not found: {executable_path}")

    if not skip_exec_validation:
        version_output = _run_pdal_version(executable_path)
        if expected_version and expected_version not in version_output:
            raise BootstrapError(
                f"Installed PDAL version does not match the manifest.\n"
                f"Expected: {expected_version}\n"
                f"pdal.exe --version: {version_output}"
            )
        print(f"Installed PDAL runtime: {version_output}")

    print(f"PDAL runtime ready: {executable_path}")
    return executable_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Download and install CSTopo's pinned Windows PDAL runtime.")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="runtime manifest JSON path")
    parser.add_argument("--runtime-zip", type=Path, help="use a local runtime ZIP instead of downloading")
    parser.add_argument("--skip-exec-validation", action="store_true", help="skip pdal.exe --version validation")
    args = parser.parse_args()

    try:
        bootstrap_pdal_runtime(
            manifest_path=args.manifest,
            runtime_zip=args.runtime_zip,
            skip_exec_validation=args.skip_exec_validation,
        )
        return 0
    except BootstrapError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
