from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]
ARCHIVE_DIR = BASE_DIR / "data" / "archive"


def list_files():
    ARCHIVE_DIR.mkdir(parents=True, exist_ok=True)
    out = []
    for p in sorted(ARCHIVE_DIR.iterdir(), key=lambda x: x.stat().st_mtime, reverse=True):
        if p.is_file():
            out.append({"name": p.name, "size": p.stat().st_size, "mtime": p.stat().st_mtime})
    return out


def get_path(name):
    p = (ARCHIVE_DIR / name).resolve()
    base = ARCHIVE_DIR.resolve()
    if not str(p).startswith(str(base)):
        return None
    return p if p.is_file() else None