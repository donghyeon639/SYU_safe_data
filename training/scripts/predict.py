"""
학습된 YOLO 가중치로 이미지 / 폴더 추론. 결과 이미지(박스 표시) 저장.

예시:
  python scripts/predict.py --weights output/yolo11m_safety/weights/best.pt --source path/to/imgs --out output/predictions --conf 0.4
"""
from __future__ import annotations

import argparse
from pathlib import Path

from ultralytics import YOLO


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--weights", required=True, type=Path)
    p.add_argument("--source", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--conf", type=float, default=0.4)
    p.add_argument("--imgsz", type=int, default=640)
    args = p.parse_args()

    model = YOLO(str(args.weights))
    args.out.mkdir(parents=True, exist_ok=True)

    model.predict(
        source=str(args.source),
        conf=args.conf,
        imgsz=args.imgsz,
        save=True,
        project=str(args.out.parent),
        name=args.out.name,
        exist_ok=True,
    )
    print(f"saved -> {args.out}")


if __name__ == "__main__":
    main()