from pydantic import BaseModel

DEFAULTS = {
    "armed": True,
    "sensitivity": 65,
    "threshold": 0.62,
    "sleep_timeout": 30,
    "watchdog": True,
    "wd_interval": 5,
    "ir": 70,
    "alarm_duration": 8,
}


class SettingsModel(BaseModel):
    armed: bool = True
    sensitivity: int = 65
    threshold: float = 0.62
    sleep_timeout: int = 30
    watchdog: bool = True
    wd_interval: int = 5
    ir: int = 70
    alarm_duration: int = 8