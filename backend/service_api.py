"""
@file service_api.py
@brief Легковесный веб-сервер для режима "Сервис" (доступ только по USB RNDIS)
"""
from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
import os
import json
import glob

app = FastAPI(title="ОВС Сервисный Интерфейс")

ARCHIVE_DIR = "archive"
CONFIG_FILE = "/etc/ovs/config.json"

# Убедимся, что директория существует (для тестов на ПК)
os.makedirs(ARCHIVE_DIR, exist_ok=True)

# --- API Эндпоинты ---

@app.get("/api/status")
def get_status():
    """Возвращает текущий статус системы и место на диске"""
    # В реальном проекте эти данные читаются из shared memory или файла, 
    # который обновляет основной C-демон. Здесь эмуляция.
    return {
        "mode": "Service (USB Connected)",
        "storage_total_mb": 32000,
        "storage_used_mb": 1250,
        "system_uptime": "14h 23m"
    }

@app.get("/api/archive")
def list_archive():
    """Возвращает список зашифрованных файлов в архиве"""
    files = []
    # Ищем все .enc файлы
    for filepath in glob.glob(os.path.join(ARCHIVE_DIR, "*.enc")):
        filename = os.path.basename(filepath)
        size = os.path.getsize(filepath)
        files.append({
            "name": filename,
            "size_kb": round(size / 1024, 2),
            "encrypted": True
        })
    return {"files": files}

@app.get("/api/archive/download/{filename}")
def download_file(filename: str):
    """Скачивание файла (в реальном проекте здесь была бы дешифровка на лету)"""
    filepath = os.path.join(ARCHIVE_DIR, filename)
    if not os.path.exists(filepath):
        raise HTTPException(status_code=404, detail="Файл не найден")
    
    # ВНИМАНИЕ: Для курсовой можно отдавать как есть, указав, что дешифровка 
    # происходит на стороне ПК уполномоченным лицом, ИЛИ добавить дешифровку здесь.
    return FileResponse(filepath, media_type='application/octet-stream', filename=filename)

@app.post("/api/settings")
def update_settings(settings: dict):
    """Сохранение новых настроек чувствительности"""
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(settings, f)
        return {"status": "success", "message": "Настройки сохранены"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# --- Раздача фронтенда ---

# Монтируем папку frontend как статическую и корневую
app.mount("/", StaticFiles(directory="frontend", html=True), name="frontend")

if __name__ == "__main__":
    import uvicorn
    # Запускаем на всех интерфейсах (включая виртуальный USB-интерфейс)
    uvicorn.run(app, host="0.0.0.0", port=8000)