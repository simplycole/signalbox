#!/usr/bin/env python3
"""Create a release tarball with stable ordering and metadata."""

from __future__ import annotations

import gzip
from pathlib import Path
import sys
import tarfile


def add_path(archive: tarfile.TarFile, path: Path, arcname: str, mtime: int) -> None:
    info = archive.gettarinfo(str(path), arcname)
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    info.mtime = mtime
    if path.is_dir():
        info.mode = 0o755
        archive.addfile(info)
    else:
        info.mode = 0o755 if path.name == "signalbox" else 0o644
        with path.open("rb") as source:
            archive.addfile(info, source)


def main() -> int:
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} STAGING-DIR ARCHIVE SOURCE-DATE-EPOCH", file=sys.stderr)
        return 2

    stage = Path(sys.argv[1]).resolve()
    output = Path(sys.argv[2]).resolve()
    mtime = int(sys.argv[3])
    if not stage.is_dir():
        raise SystemExit(f"staging directory does not exist: {stage}")

    with output.open("wb") as raw:
        with gzip.GzipFile(fileobj=raw, mode="wb", filename="", mtime=mtime) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                add_path(archive, stage, stage.name, mtime)
                for path in sorted(stage.iterdir(), key=lambda item: item.name):
                    add_path(archive, path, f"{stage.name}/{path.name}", mtime)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
