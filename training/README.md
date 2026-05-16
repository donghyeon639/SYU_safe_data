# Safety Detection - YOLO11

Unreal `ExP12` 합성 데이터로 **worker / helmet / vest** 를 한 번에 검출하는 Ultralytics YOLO11 학습 환경.

## 디렉터리

```
training/
├── README.md
├── requirements.txt
├── setup_env.ps1                   # 최초 1회: venv + ultralytics 설치
├── data.yaml                       # YOLO 데이터셋 명세 (3 classes)
├── scripts/
│   ├── train.ps1                   # 학습 (.pt 산출)
│   ├── convert_unreal_to_yolo.py   # Unreal 출력 → YOLO 라벨
│   ├── visualize.py                # 라벨 sanity check
│   └── predict.py                  # 추론
├── datasets/safety/
│   ├── images/{train,val}/         # 이미지
│   └── labels/{train,val}/         # YOLO 라벨 (.txt)
└── output/                         # 학습 결과 (best.pt 등)
```

## 1. 셋업 (최초 1회)

> Windows 처음이면: `Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned`

```powershell
cd c:\safe_data\training
.\setup_env.ps1
```

`venv` 생성 + ultralytics + opencv 설치. PyTorch 는 ultralytics 가 의존성으로 함께 설치.

## 2. 데이터 변환

Unreal 측에서 이미지 옆에 사이드카 JSON 을 떨어뜨려야 함:

```
<scene>/t0.0s_N.jpg
<scene>/t0.0s_N.json
```
```json
{
  "image": "t0.0s_N.jpg",
  "image_width": 1920,
  "image_height": 1080,
  "objects": [
    {"category": "worker", "bbox_xyxy": [x1, y1, x2, y2]}
  ]
}
```

`category` 는 `worker | helmet | vest`. 현재 worker 만 잡혀도 OK (data.yaml 은 3-class 로 미리 구성).

변환:
```powershell
.\.venv\Scripts\Activate.ps1
python scripts\convert_unreal_to_yolo.py --source ..\Demo\Windows\ExP12\Saved --output datasets\safety
```

라벨 확인 (옵션):
```powershell
python scripts\visualize.py --images datasets\safety\images\train --labels datasets\safety\labels\train --out output\viz
```

## 3. 학습

```powershell
.\scripts\train.ps1
```

내부적으로 ultralytics CLI 호출:
```
yolo detect train model=yolo11m.pt data=data.yaml epochs=60 imgsz=640 batch=4 device=0 project=output name=yolo11m_safety
```

- `yolo11{n,s,m,l,x}.pt` 중 선택 (m = balanced). 가중치는 자동 다운로드.
- 결과: `output/yolo11m_safety/weights/best.pt` (+ last.pt)
- GPU OOM 이면 `batch=2` 로 낮추기.

## 4. 추론

```powershell
python scripts\predict.py --weights output\yolo11m_safety\weights\best.pt --source <이미지경로> --out output\predictions
```

## 클래스 (0-indexed, YOLO 규약)

| id | name |
|----|------|
| 0  | worker |
| 1  | helmet |
| 2  | vest |

클래스 추가 시: `data.yaml` 의 `nc`/`names`, 그리고 `scripts/convert_unreal_to_yolo.py` 의 `NAME_TO_ID` 두 곳을 함께 수정.
