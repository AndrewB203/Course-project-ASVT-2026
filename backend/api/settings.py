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