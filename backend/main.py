import random
import time
from pathlib import Path

from fastapi import FastAPI, Request, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from api import archive, auth, events, settings, stats, system
from models.database import get_conn, init_db, _lock
from services import auth_service

app = FastAPI(title="OVS Backend", version="1.0.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Публичные эндпоинты (без проверки сессии)
PUBLIC_PREFIXES = ("/api/auth", "/api/health", "/docs", "/openapi.json", "/redoc")

# Эндпоинты только для admin
ADMIN_PREFIXES = ("/api/settings",)


def _current_user(request: Request):
    token = request.cookies.get("session")
    if not token:
        return None
    return auth_service.get_session(token)


@app.middleware("http")
async def auth_middleware(request: Request, call_next):
    path = request.url.path
    # Пропускаем публичные пути
    if any(path.startswith(p) for p in PUBLIC_PREFIXES):
        return await call_next(request)
    # Пропускаем статику
    if not path.startswith("/api/"):
        return await call_next(request)

    user = _current_user(request)
    if not user:
        from fastapi.responses import JSONResponse
        return JSONResponse({"detail": "Не авторизован"}, status_code=401)

    # Проверка прав для admin-only эндпоинтов
    if any(path.startswith(p) for p in ADMIN_PREFIXES) and request.method in ("PUT", "POST", "DELETE"):
        if user["role"] != "admin":
            from fastapi.responses import JSONResponse
            return JSONResponse({"detail": "Только для администратора"}, status_code=403)

    request.state.user = user
    return await call_next(request)


for r in (auth.router, events.router, stats.router, settings.router, archive.router, system.router):
    app.include_router(r)

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
    auth_service.seed_default_users()
    _seed_demo()