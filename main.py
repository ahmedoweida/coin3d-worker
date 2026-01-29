import os
import time
import uuid
from fastapi import FastAPI, Header, HTTPException
from pydantic import BaseModel

app = FastAPI()

API_KEY = os.environ.get("WORKER_API_KEY", "")
JOBS = {}  # demo only (memory). Replace with Redis/DB in production.


class StartJobRequest(BaseModel):
    jobId: str
    input: dict  # { type: "iv"|"zip", url: "...", filename: "..." }
    options: dict | None = None


def require_auth(authorization: str | None):
    if not API_KEY:
        raise HTTPException(status_code=500, detail="WORKER_API_KEY not set on server")
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(status_code=401, detail="Missing Bearer token")
    token = authorization.split(" ", 1)[1].strip()
    if token != API_KEY:
        raise HTTPException(status_code=403, detail="Invalid token")


@app.get("/health")
def health():
    return {"ok": True}


@app.post("/v1/jobs")
def start_job(req: StartJobRequest, authorization: str | None = Header(default=None)):
    require_auth(authorization)

    worker_job_id = str(uuid.uuid4())
    now = time.time()
    JOBS[worker_job_id] = {
        "status": "running",
        "stage": "validating",
        "progress": 5,
        "createdAt": now,
        "appJobId": req.jobId,
        "warnings": [],
        "output": None,
    }
    return {"workerJobId": worker_job_id, "jobId": req.jobId}


@app.get("/v1/jobs/{workerJobId}")
def get_job(workerJobId: str, authorization: str | None = Header(default=None)):
    require_auth(authorization)

    job = JOBS.get(workerJobId)
    if not job:
        raise HTTPException(status_code=404, detail="Job not found")

    # Demo progress simulation:
    age = time.time() - job["createdAt"]
    if job["status"] == "running":
        if age > 2:
            job["stage"] = "exporting"
            job["progress"] = 80
        if age > 4:
            job["status"] = "completed"
            job["stage"] = "completed"
            job["progress"] = 100
            job["warnings"].append("Demo worker: conversion is stubbed; no real GLB generated yet.")
            job["output"] = {
                "glbUrl": None,
                "thumbnailUrl": None,
                "metadata": {},
            }

    return {
        "status": job["status"],
        "stage": job["stage"],
        "progress": job["progress"],
        "warnings": job["warnings"],
        "output": job["output"],
    }
