"""
학습된 YOLOv11 모델 평가 및 추론 테스트.

사용법:
  python scripts/evaluate.py
  python scripts/evaluate.py --weights output/yolo11m_safety/weights/best.pt
  python scripts/evaluate.py --infer path/to/image.jpg
"""

import argparse
from pathlib import Path

try:
    from ultralytics import YOLO
except ImportError:
    print("[오류] ultralytics 미설치. pip install -r requirements.txt")
    raise SystemExit(1)

LEARN_DIR = Path(__file__).parent.parent
DEFAULT_WEIGHTS = LEARN_DIR / "output" / "yolo11m_safety" / "weights" / "best.pt"
DEFAULT_DATA = LEARN_DIR / "data.yaml"
CLASS_NAMES = {0: "worker", 1: "helmet", 2: "vest"}


def evaluate(weights: Path, data: Path) -> None:
    if not weights.exists():
        print(f"[오류] 가중치 없음: {weights}")
        print("먼저 scripts\\train.ps1 을 실행하세요.")
        return

    model = YOLO(str(weights))
    metrics = model.val(data=str(data), split="val")

    print("\n=== 평가 결과 ===")
    print(f"  mAP@50     : {metrics.box.map50:.4f}  (목표: 0.85 이상)")
    print(f"  mAP@50-95  : {metrics.box.map:.4f}   (목표: 0.65 이상)")
    print(f"  Precision  : {metrics.box.mp:.4f}")
    print(f"  Recall     : {metrics.box.mr:.4f}   (안전 시스템 최우선 지표)")

    if metrics.box.mr < 0.85:
        print("\n[경고] Recall 낮음 → 작업자 미탐지 위험")
        print("  - 데이터 추가 수집 또는 confidence threshold 조정을 권장합니다.")


def infer(weights: Path, img_path: str) -> None:
    if not weights.exists():
        print(f"[오류] 가중치 없음: {weights}")
        return

    model = YOLO(str(weights))
    results = model.predict(
        source=img_path,
        conf=0.5,
        save=True,
        project=str(LEARN_DIR / "output" / "infer"),
    )

    print(f"\n=== 추론 결과: {img_path} ===")
    for result in results:
        for box in result.boxes:
            cls = int(box.cls)
            conf = float(box.conf)
            label = CLASS_NAMES.get(cls, f"class{cls}")
            print(f"  {label}  신뢰도: {conf:.2%}")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--weights", type=Path, default=DEFAULT_WEIGHTS,
                   help="모델 가중치 경로 (기본값: output/yolo11m_safety/weights/best.pt)")
    p.add_argument("--data", type=Path, default=DEFAULT_DATA,
                   help="data yaml 경로 (기본값: data.yaml)")
    p.add_argument("--infer", metavar="IMAGE", help="단일 이미지 추론 테스트")
    args = p.parse_args()

    if args.infer:
        infer(args.weights, args.infer)
    else:
        evaluate(args.weights, args.data)


if __name__ == "__main__":
    main()
