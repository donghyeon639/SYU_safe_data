# YOLOv11 건설현장 안전 감지 모델

Unreal Engine 5로 생성한 합성 이미지를 활용해 **worker / helmet / vest** 를 탐지하는 YOLOv11 모델을 학습합니다.

---

## 요구사항

| 항목 | 버전 |
|------|------|
| Python | 3.10 이상 |
| CUDA (권장) | 11.8 / 12.1 / 12.4 |
| GPU | NVIDIA (CPU 전용도 가능, 속도 느림) |

---

## 설치

```powershell
git clone <repo-url>
cd learn
.\setup_env.ps1
```

CUDA 버전 직접 지정:

```powershell
.\setup_env.ps1 -CudaVersion cu118   # CUDA 11.8
.\setup_env.ps1 -CudaVersion cu124   # CUDA 12.4
.\setup_env.ps1 -CudaVersion cpu     # GPU 없음
```

---

## 파이프라인 테스트

더미 데이터로 전체 파이프라인(변환 → 시각화 → 학습 → 평가)을 검증합니다.

```powershell
.\scripts\test_pipeline.ps1
```

---

## 학습

### 1. 데이터 변환

UE5 Saved 폴더를 YOLO 포맷으로 변환합니다.

```powershell
.venv\Scripts\Activate.ps1
python scripts\convert_unreal_to_yolo.py `
    --source D:\exP12\Saved `
    --output datasets\safety
```

### 2. 학습 실행

```powershell
.\scripts\train.ps1
```

모델 크기 변경 시 `scripts\train.ps1` 안의 `yolo11m.pt` 를 `yolo11n/s/l/x.pt` 로 수정하세요.

### 3. 평가 / 추론

```powershell
python scripts\evaluate.py

python scripts\predict.py `
    --weights output\yolo11m_safety\weights\best.pt `
    --source path\to\images `
    --out output\predictions
```

---

## 스크립트 목록

| 파일 | 역할 |
|------|------|
| `setup_env.ps1` | 가상환경 생성 + PyTorch/패키지 설치 |
| `scripts/test_pipeline.ps1` | 더미 데이터로 전체 파이프라인 검증 |
| `scripts/train.ps1` | YOLO 학습 실행 |
| `scripts/convert_unreal_to_yolo.py` | UE5 JSON → YOLO 포맷 변환 |
| `scripts/visualize.py` | 레이블 bbox 시각화 |
| `scripts/evaluate.py` | mAP / Precision / Recall 평가 |
| `scripts/predict.py` | 이미지/폴더 배치 추론 |
| `scripts/watch_ue5.py` | UE5 폴더 실시간 감시 + 자동 변환 |

---

## 클래스 정의

| ID | 이름 |
|----|------|
| 0 | worker |
| 1 | helmet |
| 2 | vest |
