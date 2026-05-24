<#
.SYNOPSIS
  learn/ 프로젝트 환경 설치 스크립트.
  가상환경 생성 → PyTorch(CUDA) 설치 → 패키지 설치 순서로 진행합니다.

.PARAMETER CudaVersion
  PyTorch CUDA 버전 (기본: 자동 감지).
  CUDA 11.8: cu118 / CUDA 12.1: cu121 / CUDA 12.4: cu124 / GPU 없음: cpu

.EXAMPLE
  .\setup_env.ps1
  .\setup_env.ps1 -CudaVersion cu118
  .\setup_env.ps1 -CudaVersion cpu
#>
param(
    [string]$CudaVersion = ""
)

$ErrorActionPreference = "Stop"
$LEARN_DIR = Split-Path -Parent $MyInvocation.MyCommand.Definition
$VENV = "$LEARN_DIR\.venv"

Set-Location $LEARN_DIR

# ── CUDA 자동 감지 ────────────────────────────────────────────────────────────
if (-not $CudaVersion) {
    $nvCmd = Get-Command "nvidia-smi" -ErrorAction SilentlyContinue
    if ($nvCmd) {
        Write-Host "GPU 감지됨 → CUDA 12.1 용 PyTorch 설치" -ForegroundColor Cyan
        $CudaVersion = "cu121"
    } else {
        Write-Host "nvidia-smi 없음 → CPU 전용 PyTorch 설치" -ForegroundColor Yellow
        Write-Host "CUDA GPU 가 있는데 이 메시지가 보이면: .\setup_env.ps1 -CudaVersion cu121"
        $CudaVersion = "cpu"
    }
}

# ── Python 확인 ───────────────────────────────────────────────────────────────
$pyCmd = Get-Command "python" -ErrorAction SilentlyContinue
if (-not $pyCmd) {
    Write-Error "[오류] Python 을 찾을 수 없습니다. Python 3.10 이상을 설치 후 다시 실행하세요."
    exit 1
}
$pyVer = & python --version
Write-Host "Python: $pyVer"

# ── 1. 가상환경 생성 ──────────────────────────────────────────────────────────
Write-Host "`n[1/4] 가상환경..."
if (Test-Path "$VENV\Scripts\Activate.ps1") {
    Write-Host "  기존 가상환경 재사용: $VENV"
} else {
    Write-Host "  생성 중: $VENV"
    python -m venv $VENV
}
& "$VENV\Scripts\Activate.ps1"

# ── 2. PyTorch 설치 ───────────────────────────────────────────────────────────
Write-Host "`n[2/4] PyTorch ($CudaVersion)..."
$torchOk = $false
try {
    $tv = & python -c "import torch; print(torch.__version__)"
    Write-Host "  이미 설치됨: $tv (건너뜀)"
    $torchOk = $true
} catch {}

if (-not $torchOk) {
    if ($CudaVersion -eq "cpu") {
        pip install torch torchvision
    } else {
        pip install torch torchvision --index-url "https://download.pytorch.org/whl/$CudaVersion"
    }
}

# ── 3. requirements.txt 설치 ──────────────────────────────────────────────────
Write-Host "`n[3/4] requirements.txt..."
pip install -r requirements.txt

# ── 4. 설치 확인 ──────────────────────────────────────────────────────────────
Write-Host "`n[4/4] 설치 확인..."
python -c "import torch; cuda=torch.cuda.is_available(); print('PyTorch', torch.__version__, '| CUDA', cuda)"
python -c "import ultralytics; print('Ultralytics', ultralytics.__version__)"
python -c "import cv2; print('OpenCV', cv2.__version__)"
python -c "import watchdog; print('watchdog OK')"

Write-Host "`n=== 환경 설치 완료 ===" -ForegroundColor Green
Write-Host ""
Write-Host "다음 단계:"
Write-Host "  파이프라인 테스트     -> .\scripts\test_pipeline.ps1"
Write-Host "  실제 UE5 데이터 변환  -> python scripts\convert_unreal_to_yolo.py --source <경로> --output datasets\safety"
Write-Host "  학습                  -> .\scripts\train.ps1"
