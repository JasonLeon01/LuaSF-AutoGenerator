from __future__ import annotations

import shutil
from pathlib import Path


def write_text_if_changed(path: Path, content: str, encoding: str = "utf-8") -> None:
    data = content.encode(encoding)
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def copy_file_if_changed(source: str | Path, destination: str | Path) -> str:
    source = Path(source)
    destination = Path(destination)
    if not destination.is_file() or source.read_bytes() != destination.read_bytes():
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    shutil.copymode(source, destination)
    return str(destination)
