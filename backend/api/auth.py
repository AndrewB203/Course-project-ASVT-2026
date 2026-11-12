from fastapi import APIRouter, HTTPException, Request, Response
from pydantic import BaseModel

from services import auth_service

router = APIRouter(prefix="/api/auth", tags=["auth"])


class LoginIn(BaseModel):
    username: str
    password: str


@router.post("/login")
def login(body: LoginIn, response: Response):
    user = auth_service.verify_user(body.username, body.password)
    if not user:
        raise HTTPException(401, "Неверный логин или пароль")
    token = auth_service.create_session(user["username"], user["role"])
    response.set_cookie(
        "session",
        token,
        httponly=True,
        samesite="lax",
        max_age=24 * 3600,
        path="/",
    )
    return {"username": user["username"], "role": user["role"]}


@router.post("/logout")
def logout(request: Request, response: Response):
    token = request.cookies.get("session")
    if token:
        auth_service.revoke_session(token)
    response.delete_cookie("session", path="/")
    return {"ok": True}


@router.get("/me")
def me(request: Request):
    token = request.cookies.get("session")
    if not token:
        raise HTTPException(401, "Не авторизован")
    session = auth_service.get_session(token)
    if not session:
        raise HTTPException(401, "Сессия истекла")
    return session


@router.get("/users")
def list_users(request: Request):
    token = request.cookies.get("session")
    session = auth_service.get_session(token) if token else None
    if not session or session["role"] != "admin":
        raise HTTPException(403, "Только для администратора")
    return auth_service.list_users()