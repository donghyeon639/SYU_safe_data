# YOLOv11 안전작업자 모델 학습
# 모델 크기 변경: model=yolo11{n,s,m,l,x}.pt
$ErrorActionPreference = "Stop"
$LEARN_DIR = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition)
$VENV = "$LEARN_DIR\.venv"

if (-not (Test-Path "$VENV\Scripts\Activate.ps1")) {
    Write-Error "[오류] 가상환경이 없습니다. 먼저 .\setup_env.ps1 을 실행하세요."
    exit 1
}

Set-Location $LEARN_DIR
& "$VENV\Scripts\Activate.ps1"

$device = python -c "import torch; print('0' if torch.cuda.is_available() else 'cpu')"
Write-Host "학습 device: $device"

yolo detect train `
    model=yolo11m.pt `
    data=data.yaml `
    epochs=60 `
    imgsz=640 `
    batch=4 `
    device=$device `
    project=output `
    name=yolo11m_safety `
    exist_ok=True
