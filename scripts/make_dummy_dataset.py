"""
테스트용 더미 YOLO 데이터셋 생성.
실제 UE5 데이터 없이 파이프라인(학습/평가) 동작을 검증할 때 사용.

사용법:
  python scripts/make_dummy_dataset.py
  python scripts/make_dummy_dataset.py --out datasets/safety_test --n-train 80 --n-val 20
"""
from __future__ import annotations

import argparse
import random
from pathlib import Path

from PIL import Image, ImageDraw

COLORS = {0: (80, 200, 80), 1: (255, 160, 40), 2: (200, 80, 200)}
IMG_SIZE = 640


def random_box() -> tuple[float, float, float, float]:
    w = random.uniform(0.05, 0.25)
    h = random.uniform(0.05, 0.25)
    xc = random.uniform(w / 2, 1 - w / 2)
    yc = random.uniform(h / 2, 1 - h / 2)
    return xc, yc, w, h


def make_sample(n_objects: int) -> tuple[Image.Image, list[str]]:
    bg = tuple(random.randint(60, 160) for _ in range(3))
    img = Image.new("RGB", (IMG_SIZE, IMG_SIZE), bg)
    draw = ImageDraw.Draw(img)
    lines: list[str] = []
    for _ in range(n_objects):
        cls = random.randint(0, 2)
        xc, yc, w, h = random_box()
        x1 = int((xc - w / 2) * IMG_SIZE)
        y1 = int((yc - h / 2) * IMG_SIZE)
        x2 = int((xc + w / 2) * IMG_SIZE)
        y2 = int((yc + h / 2) * IMG_SIZE)
        draw.rectangle([x1, y1, x2, y2], fill=COLORS[cls], outline=(0, 0, 0), width=3)
        lines.append(f"{cls} {xc:.6f} {yc:.6f} {w:.6f} {h:.6f}")
    return img, lines


def write_split(img_dir: Path, lbl_dir: Path, n: int, prefix: str) -> None:
    img_dir.mkdir(parents=True, exist_ok=True)
    lbl_dir.mkdir(parents=True, exist_ok=True)
    for i in range(n):
        name = f"{prefix}_{i:04d}"
        img, lines = make_sample(random.randint(1, 4))
        img.save(img_dir / f"{name}.jpg")
        (lbl_dir / f"{name}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, default=Path("datasets/safety_test"))
    p.add_argument("--n-train", type=int, default=100)
    p.add_argument("--n-val", type=int, default=20)
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    random.seed(args.seed)

    out: Path = args.out
    print(f"더미 데이터셋 생성 중: {out}")
    write_split(out / "images/train", out / "labels/train", args.n_train, "train")
    write_split(out / "images/val", out / "labels/val", args.n_val, "val")
    print(f"  train {args.n_train}장 / val {args.n_val}장 완료")


if __name__ == "__main__":
    main()
