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