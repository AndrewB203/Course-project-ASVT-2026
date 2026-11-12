from typing import Optional
from pydantic import BaseModel


class EventIn(BaseModel):
    type: str
    severity: str = "info"
    title: str
    detail: str = ""
    zone: str = ""
    conf: Optional[float] = None
    note: str = ""


class EventOut(EventIn):
    id: int
    ts: float