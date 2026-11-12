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