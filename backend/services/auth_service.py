import hashlib
import secrets
import time
from typing import Optional

from models.database import get_conn, _lock

# In-memory sessions: token -> {username, role, expires}
_sessions = {}
_SESSION_TTL = 24 * 3600  # 24 часа


def _hash_password(password: str, salt: str) -> str:
    return hashlib.pbkdf2_hmac(
        "sha256", password.encode("utf-8"), salt.encode("utf-8"), 100_000
    ).hex()


def create_user(username: str, password: str, role: str = "user") -> bool:
    salt = secrets.token_hex(16)
    h = _hash_password(password, salt)
    with _lock, get_conn() as c:
        try:
            c.execute(
                "INSERT INTO users(username, password_hash, salt, role, created_at)"
                " VALUES(?,?,?,?,?)",
                (username, h, salt, role, time.time()),
            )
            return True
        except sqlite3.IntegrityError:
            return False


def verify_user(username: str, password: str) -> Optional[dict]:
    with _lock, get_conn() as c:
        row = c.execute(
            "SELECT id, username, password_hash, salt, role FROM users WHERE username=?",
            (username,),
        ).fetchone()
    if not row:
        return None
    if _hash_password(password, row["salt"]) != row["password_hash"]:
        return None
    return {"id": row["id"], "username": row["username"], "role": row["role"]}


def create_session(username: str, role: str) -> str:
    token = secrets.token_urlsafe(32)
    _sessions[token] = {
        "username": username,
        "role": role,
        "expires": time.time() + _SESSION_TTL,
    }
    return token


def get_session(token: str) -> Optional[dict]:
    s = _sessions.get(token)
    if not s:
        return None
    if s["expires"] < time.time():
        _sessions.pop(token, None)
        return None
    return {"username": s["username"], "role": s["role"]}


def revoke_session(token: str):
    _sessions.pop(token, None)


def list_users():
    with _lock, get_conn() as c:
        rows = c.execute("SELECT id, username, role, created_at FROM users").fetchall()
    return [dict(r) for r in rows]


def seed_default_users():
    """Создаёт admin/admin и user/user при первом запуске."""
    with _lock, get_conn() as c:
        n = c.execute("SELECT COUNT(*) FROM users").fetchone()[0]
    if n == 0:
        create_user("admin", "admin", role="admin")
        create_user("user", "user", role="user")