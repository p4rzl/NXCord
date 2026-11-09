#!/usr/bin/env python3
"""Package matched NRO/sysmodule/overlay artifacts without touching the SD logs."""
from argparse import ArgumentParser
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

parser = ArgumentParser()
parser.add_argument("--build", type=Path, default=Path("build"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = args.build.resolve()
files = {
    build / "client.nro": "switch/nxcord.nro",
    build / "overlay.ovl": "switch/.overlays/nxcord.ovl",
    build / "sysmodule.nsp": "atmosphere/contents/5600000000000000/exefs.nsp",
    root / "sysmodule/toolbox.json": "atmosphere/contents/5600000000000000/toolbox.json",
}
for name in ("libdave", "mlspp", "nlohmann-json", "openssl"):
    files[build / f"licenses/{name}.txt"] = f"licenses/{name}.txt"
files[root / "docs/DAVE-TESTING.md"] = "DAVE-TESTING.md"
for source in files:
    if not source.is_file():
        raise SystemExit(f"Missing build artifact/license: {source}")
archive_path = build / "release.zip"
with ZipFile(archive_path, "w", ZIP_DEFLATED) as archive:
    for source, target in files.items():
        archive.write(source, target)
    archive.writestr("atmosphere/contents/5600000000000000/flags/boot2.flag", b"")
with ZipFile(archive_path) as archive:
    assert archive.testzip() is None
    for source, target in files.items():
        assert archive.read(target) == source.read_bytes()
print(f"Verified experimental DAVE package: {archive_path} ({archive_path.stat().st_size} bytes)")
