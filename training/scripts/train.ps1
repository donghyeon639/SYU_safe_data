# Train Ultralytics YOLO11 on safety dataset.
# 모델 크기 바꾸려면 model=yolo11{n,s,m,l,x}.pt 변경.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition))

& .\.venv\Scripts\Activate.ps1
yolo detect train `
    model=yolo11m.pt `
    data=data.yaml `
    epochs=60 `
    imgsz=640 `
    batch=4 `
    device=0 `
    project=output `
    name=yolo11m_safety