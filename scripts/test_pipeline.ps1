# 파이프라인 전체 검증 (더미 데이터 기반, 약 5~10분 소요)
# 실제 UE5 데이터 없이 convert → visualize → train → evaluate 흐름을 확인합니다.
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
Write-Host "device: $device`n"

# 1. 더미 데이터셋 생성
Write-Host "[1/4] 더미 데이터셋 생성..."
python scripts\make_dummy_dataset.py

# 2. 레이블 시각화 (5장 샘플)
Write-Host "`n[2/4] 레이블 시각화..."
python scripts\visualize.py `
    --images datasets\safety_test\images\train `
    --labels datasets\safety_test\labels\train `
    --out output\viz_test `
    --num 5
Write-Host "  확인: output\viz_test\"

# 3. 단기 학습 (nano 모델, 3 epoch — 파이프라인 동작만 검증)
Write-Host "`n[3/4] 단기 학습 (yolo11n, 3 epoch)..."
yolo detect train `
    model=yolo11n.pt `
    data=data_test.yaml `
    epochs=3 `
    imgsz=320 `
    batch=8 `
    device=$device `
    project=output `
    name=test_run `
    exist_ok=True

# 4. 평가 (더미 데이터이므로 mAP 수치는 무의미 — 오류 없이 실행되는지만 확인)
Write-Host "`n[4/4] 평가..."
python scripts\evaluate.py `
    --weights output\test_run\weights\best.pt `
    --data data_test.yaml

Write-Host "`n=== 파이프라인 테스트 완료 ===" -ForegroundColor Green
Write-Host "실제 UE5 데이터로 학습하려면 -> .\scripts\train.ps1"
