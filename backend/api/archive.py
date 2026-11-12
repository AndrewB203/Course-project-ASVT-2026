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