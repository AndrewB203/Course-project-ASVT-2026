#requires -version 5.1
# Generates the full OVS backend under backend/. Never overwrites non-empty files.
[CmdletBinding()]
param([string]$BackendDir)
$ErrorActionPreference = 'Stop'
$ProjectRoot = if ($PSScriptRoot) { Split-Path $PSScriptRoot -Parent } else { (Get-Location).Path }

$Backend = $null
if ($BackendDir) { $Backend = (Resolve-Path $BackendDir).Path }
else {
    foreach ($c in @((Join-Path $ProjectRoot 'backend'), $ProjectRoot, (Get-Location).Path)) {
        if ((Test-Path (Join-Path $c 'api')) -and (Test-Path (Join-Path $c 'main.py'))) { $Backend = $c; break }
    }
}
if (-not $Backend) { throw 'backend dir not found (need api/ and main.py)' }
Write-Host "Backend dir: $Backend" -ForegroundColor Cyan

function Write-BackendFile {
    param([string]$Rel, [string]$Content)
    $p = Join-Path $Backend $Rel
    if ((Test-Path $p) -and ((Get-Item $p).Length -gt 0)) {
        Write-Host "[SKIP] $Rel (already has code)" -ForegroundColor Yellow
        return
    }
    New-Item -ItemType Directory -Path (Split-Path $p -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($p, $Content, [Text.UTF8Encoding]::new($false))
    Write-Host "[OK  ] $Rel" -ForegroundColor Green
}

Write-BackendFile 'requirements.txt' @'
fastapi>=0.110
uvicorn[standard]>=0.29
pydantic>=2.6
'@

Write-BackendFile 'requirments.txt' @'
fastapi>=0.110
uvicorn[standard]>=0.29
pydantic>=2.6
'@

Write-BackendFile 'Dockerfile' @'
FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY . .
EXPOSE 8000
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
'@

Write-BackendFile 'run.sh' @'
#!/bin/sh
cd "$(dirname "$0")"
exec uvicorn main:app --host 0.0.0.0 --port 8000
'@

Write-BackendFile 'models\database.py' @'
import sqlite3
import threading
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]
DATA_DIR = BASE_DIR / "data"
DB_PATH = DATA_DIR / "ovs.db"
_lock = threading.Lock()


def get_conn():
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH), check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    with _lock, get_conn() as c:
        c.execute(
            """CREATE TABLE IF NOT EXISTS events(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts REAL NOT NULL,
                type TEXT NOT NULL,
                severity TEXT NOT NULL DEFAULT 'info',
                title TEXT NOT NULL,
                detail TEXT DEFAULT '',
                zone TEXT DEFAULT '',
                conf REAL,
                note TEXT DEFAULT '')"""
        )
        c.execute("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT)")
        c.execute("CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts)")
'@

Write-BackendFile 'models\event.py' @'
from typing import Optional
from pydantic import BaseModel


class EventIn(BaseModel):
    type: str
    severity: str = "info"
    title: str
    detail: str = ""
    zone: str = ""
    conf: Optional[float] = None
    note: str = ""


class EventOut(EventIn):
    id: int
    ts: float
'@

Write-BackendFile 'models\settings.py' @'
from pydantic import BaseModel

DEFAULTS = {
    "armed": True,
    "sensitivity": 65,
    "threshold": 0.62,
    "sleep_timeout": 30,
    "watchdog": True,
    "wd_interval": 5,
    "ir": 70,
    "alarm_duration": 8,
}


class SettingsModel(BaseModel):
    armed: bool = True
    sensitivity: int = 65
    threshold: float = 0.62
    sleep_timeout: int = 30
    watchdog: bool = True
    wd_interval: int = 5
    ir: int = 70
    alarm_duration: int = 8
'@

Write-BackendFile 'services\config_service.py' @'
import json
from models.database import get_conn, _lock
from models.settings import DEFAULTS


def get_settings():
    with _lock, get_conn() as c:
        rows = c.execute("SELECT key, value FROM settings").fetchall()
    data = dict(DEFAULTS)
    for r in rows:
        data[r["key"]] = json.loads(r["value"])
    return data


def set_settings(patch):
    with _lock, get_conn() as c:
        for k, v in patch.items():
            c.execute(
                "INSERT INTO settings(key, value) VALUES(?, ?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                (k, json.dumps(v)),
            )
    return get_settings()
'@

Write-BackendFile 'services\archive_service.py' @'
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
'@

Write-BackendFile 'services\decryption_service.py' @'
"""Archive decryption stub.

Replace the body with the real algorithm (e.g. AES-GCM from the cryptography
package) once the encryption side is implemented on the device.
"""


def decrypt_blob(data: bytes, key: bytes = b"") -> bytes:
    return data


def is_encrypted(name: str) -> bool:
    return name.endswith(".enc")
'@

Write-BackendFile 'api\events.py' @'
import time
from fastapi import APIRouter, HTTPException
from models.database import get_conn, _lock
from models.event import EventIn

router = APIRouter(prefix="/api/events", tags=["events"])


@router.get("")
def list_events(limit: int = 200, type: str = None, severity: str = None):
    q = "SELECT * FROM events WHERE 1=1"
    args = []
    if type:
        q += " AND type=?"
        args.append(type)
    if severity:
        q += " AND severity=?"
        args.append(severity)
    q += " ORDER BY ts DESC LIMIT ?"
    args.append(limit)
    with _lock, get_conn() as c:
        rows = c.execute(q, args).fetchall()
    return [dict(r) for r in rows]


@router.post("")
def add_event(e: EventIn):
    with _lock, get_conn() as c:
        cur = c.execute(
            "INSERT INTO events(ts, type, severity, title, detail, zone, conf, note)"
            " VALUES(?,?,?,?,?,?,?,?)",
            (time.time(), e.type, e.severity, e.title, e.detail, e.zone, e.conf, e.note),
        )
        return {"id": cur.lastrowid}


@router.delete("/{event_id}")
def delete_event(event_id: int):
    with _lock, get_conn() as c:
        cur = c.execute("DELETE FROM events WHERE id=?", (event_id,))
        if cur.rowcount == 0:
            raise HTTPException(404, "event not found")
    return {"deleted": event_id}
'@

Write-BackendFile 'api\stats.py' @'
import time
from fastapi import APIRouter
from models.database import get_conn, _lock

router = APIRouter(prefix="/api/stats", tags=["stats"])


@router.get("")
def stats():
    day_ago = time.time() - 86400
    with _lock, get_conn() as c:
        total = c.execute("SELECT COUNT(*) FROM events").fetchone()[0]
        last24 = c.execute("SELECT COUNT(*) FROM events WHERE ts > ?", (day_ago,)).fetchone()[0]
        by_type = c.execute("SELECT type, COUNT(*) n FROM events GROUP BY type").fetchall()
        by_sev = c.execute("SELECT severity, COUNT(*) n FROM events GROUP BY severity").fetchall()
    return {
        "total": total,
        "last24h": last24,
        "by_type": {r["type"]: r["n"] for r in by_type},
        "by_severity": {r["severity"]: r["n"] for r in by_sev},
    }
'@

Write-BackendFile 'api\settings.py' @'
from typing import Any, Dict
from fastapi import APIRouter
from services import config_service

router = APIRouter(prefix="/api/settings", tags=["settings"])


@router.get("")
def get_settings():
    return config_service.get_settings()


@router.put("")
def put_settings(patch: Dict[str, Any]):
    return config_service.set_settings(patch)
'@

Write-BackendFile 'api\archive.py' @'
from fastapi import APIRouter, HTTPException
from fastapi.responses import FileResponse
from services import archive_service

router = APIRouter(prefix="/api/archive", tags=["archive"])


@router.get("")
def list_archive():
    return archive_service.list_files()


@router.get("/download/{name}")
def download(name: str):
    p = archive_service.get_path(name)
    if not p:
        raise HTTPException(404, "file not found")
    return FileResponse(str(p), filename=name)
'@

Write-BackendFile 'api\system.py' @'
import os
import platform
import shutil
import time
from fastapi import APIRouter

router = APIRouter(prefix="/api/system", tags=["system"])
BOOT = time.time()


def _cpu_temp():
    try:
        with open("/sys/class/thermal/thermal_zone0/temp") as f:
            return round(int(f.read().strip()) / 1000.0, 1)
    except Exception:
        return None


@router.get("/status")
def status():
    return {
        "platform": platform.system(),
        "machine": platform.machine(),
        "uptime": int(time.time() - BOOT),
        "pid": os.getpid(),
        "cpu_temp": _cpu_temp(),
        "disk_free": shutil.disk_usage(str(os.getcwd())).free,
    }
'@

Write-BackendFile 'main.py' @'
import random
import time
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from api import archive, events, settings, stats, system
from models.database import get_conn, init_db, _lock

app = FastAPI(title="OVS Backend", version="1.0.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)
for r in (events.router, stats.router, settings.router, archive.router, system.router):
    app.include_router(r)

# Integrate the pre-existing service_api module if it exposes router or app.
try:
    import service_api
    if hasattr(service_api, "router"):
        app.include_router(service_api.router)
    elif hasattr(service_api, "app"):
        app.mount("/service", service_api.app, name="service_api")
except Exception:
    pass


@app.get("/api/health")
def health():
    return {"status": "ok", "ts": time.time()}


def _seed_demo():
    with _lock, get_conn() as c:
        if c.execute("SELECT COUNT(*) FROM events").fetchone()[0]:
            return
        now = time.time()
        demo = [
            (300, "system", "info", "System boot", "Buildroot 2023.02.6, kernel 5.10.160", "", None),
            (299, "system", "info", "Daemon started (PID 894)", "ovs_power_daemon, static build", "", None),
            (240, "motion", "warn", "Wake on GPIO", "Motion sensor, zone 1", "Zone 1 - Entry", None),
            (239, "person", "crit", "Person detected", "RKNN inference: class person", "Zone 1 - Entry", 0.91),
            (239, "alarm", "crit", "Alarm triggered", "Siren and event recording", "Zone 1 - Entry", None),
            (231, "alarm", "info", "Alarm cleared", "Timeout 8 s, back to active", "", None),
            (180, "power", "info", "Sleep mode", "Idle timeout 30 s", "", None),
            (120, "person", "crit", "Person detected", "RKNN inference: class person", "Zone 2 - Perimeter", 0.87),
            (60, "watchdog", "info", "Watchdog heartbeat OK", "Daemon replied in 4 ms", "", None),
            (30, "system", "info", "Config applied", "sensitivity=65, threshold=0.62", "", None),
        ]
        for off, typ, sev, title, detail, zone, conf in demo:
            c.execute(
                "INSERT INTO events(ts, type, severity, title, detail, zone, conf, note)"
                " VALUES(?,?,?,?,?,?,?,?)",
                (now - off + random.random(), typ, sev, title, detail, zone, conf, ""),
            )


@app.on_event("startup")
def startup():
    init_db()
    _seed_demo()
    fe = Path(__file__).resolve().parents[1] / "frontend"
    if fe.exists() and (fe / "index.html").exists():
        app.mount("/", StaticFiles(directory=str(fe), html=True), name="frontend")
'@

Write-Host ''
Write-Host 'Backend generated. Run:' -ForegroundColor Cyan
Write-Host ("  cd " + $Backend) -ForegroundColor White
Write-Host '  pip install -r requirements.txt' -ForegroundColor White
Write-Host '  python -m uvicorn main:app --reload --port 8000 --host 0.0.0.0' -ForegroundColor White