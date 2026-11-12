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