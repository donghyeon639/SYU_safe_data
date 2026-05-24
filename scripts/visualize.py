"""
YOLO 레이블 검증: 바운딩박스를 이미지 위에 그려 저장.
convert_unreal_to_yolo.py 실행 후 레이블이 올바른지 확인할 때 사용.

사용법:
  python scripts/visualize.py --images datasets/safety/images/train --labels datasets/safety/labels/train --out output/viz
  python scripts/visualize.py --images datasets/safety/images/train --labels datasets/safety/labels/train --out output/viz --num 20
"""
from __future__ import annotations

import argparse
import random
from pathlib import Path

import cv2

NAMES = {0: "worker", 1: "helmet", 2: "vest"}
COLORS = {0: (0, 255, 0), 1: (0, 165, 255), 2: (255, 0, 255)}  # 초록, 주황, 자홍


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--images", required=True, type=Path)
    p.add_argument("--labels", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--num", type=int, default=20, help="샘플링할 이미지 수 (0=전체)")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()

    imgs = sorted(p for p in args.images.iterdir() if p.suffix.lower() in (".jpg", ".jpeg", ".png"))
    if args.num and args.num < len(imgs):
        imgs = random.Random(args.seed).sample(imgs, args.num)
    args.out.mkdir(parents=True, exist_ok=True)

    for img_path in imgs:
        im = cv2.imread(str(img_path))
        if im is None:
            continue
        h, w = im.shape[:2]
        lbl = args.labels / (img_path.stem + ".txt")
        if lbl.is_file():
            for line in lbl.read_text(encoding="utf-8").splitlines():
                parts = line.split()
                if len(parts) != 5:
                    continue
                cid, xc, yc, bw, bh = int(parts[0]), *map(float, parts[1:])
                x1 = int((xc - bw / 2) * w)
                y1 = int((yc - bh / 2) * h)
                x2 = int((xc + bw / 2) * w)
                y2 = int((yc + bh / 2) * h)
                color = COLORS.get(cid, (255, 255, 255))
                cv2.rectangle(im, (x1, y1), (x2, y2), color, 2)
                cv2.putText(im, NAMES.get(cid, str(cid)), (x1, max(0, y1 - 5)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2, cv2.LINE_AA)
        cv2.imwrite(str(args.out / img_path.name), im)

    print(f"{len(imgs)}장 저장 → {args.out}")


if __name__ == "__main__":
    main()
