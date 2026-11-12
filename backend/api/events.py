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