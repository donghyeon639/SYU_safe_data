"""
Unreal ExP12 캡쳐 결과 → Ultralytics YOLO 포맷 변환기.

입력 (이미지별 사이드카 JSON):
  <scene>/t0.0s_N.jpg
  <scene>/t0.0s_N.json
    {
      "image": "t0.0s_N.jpg",
      "image_width": 1920,
      "image_height": 1080,
      "objects": [
        {"category": "worker", "bbox_xyxy": [x1, y1, x2, y2]}
      ]
    }

또는 <scene>/metadata.json 안에 image_annotations 임베드도 지원.

출력 (YOLO 포맷):
  datasets/safety/
    images/{train,val}/<scene>__<image>.jpg
    labels/{train,val}/<scene>__<image>.txt   각 라인: "<class_id> <xc> <yc> <w> <h>" (normalized 0~1)
"""
from __future__ import annotations

import argparse
import json
import random
import shutil
import sys
from pathlib import Path
from typing import Iterable

from PIL import Image
from tqdm import tqdm

# 0-indexed (YOLO 규약), data.yaml 의 names 와 매칭
NAME_TO_ID = {"worker": 0, "helmet": 1, "vest": 2}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--source", required=True, type=Path,
                   help="ExP12\\Saved 경로 (AccidentScreenshots/, NormalScreenshots/ 가 그 안에 있음)")
    p.add_argument("--output", required=True, type=Path,
                   help="datasets/safety 등 출력 루트")
    p.add_argument("--val-ratio", type=float, default=0.1)
    p.add_argument("--seed", type=int, default=42)
    return p.parse_args()


def iter_scenes(source: Path) -> Iterable[Path]:
    for sub in ("AccidentScreenshots", "NormalScreenshots"):
        root = source / sub
        if not root.is_dir():
            continue
        for child in sorted(root.iterdir()):
            if child.is_dir():
                yield child


def load_embedded(scene: Path) -> dict[str, dict]:
    meta = scene / "metadata.json"
    if not meta.is_file():
        return {}
    try:
        data = json.loads(meta.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"  ! {meta} parse failed: {e}", file=sys.stderr)
        return {}
    ann = data.get("image_annotations")
    return ann if isinstance(ann, dict) else {}


def load_sidecar(image_path: Path) -> dict | None:
    sidecar = image_path.with_suffix(".json")
    if not sidecar.is_file():
        return None
    try:
        return json.loads(sidecar.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"  ! {sidecar} parse failed: {e}", file=sys.stderr)
        return None


def to_yolo_line(category: str, bbox_xyxy, img_w: int, img_h: int) -> str | None:
    """Convert one (category, xyxy pixel bbox) -> YOLO 'cls xc yc w h' normalized."""
    if category not in NAME_TO_ID:
        return None
    if not (isinstance(bbox_xyxy, (list, tuple)) and len(bbox_xyxy) == 4):
        return None
    x1, y1, x2, y2 = map(float, bbox_xyxy)
    # clip
    x1 = max(0.0, min(x1, img_w))
    y1 = max(0.0, min(y1, img_h))
    x2 = max(0.0, min(x2, img_w))
    y2 = max(0.0, min(y2, img_h))
    bw, bh = x2 - x1, y2 - y1
    if bw <= 1 or bh <= 1:
        return None
    xc = (x1 + x2) / 2.0 / img_w
    yc = (y1 + y2) / 2.0 / img_h
    nw = bw / img_w
    nh = bh / img_h
    return f"{NAME_TO_ID[category]} {xc:.6f} {yc:.6f} {nw:.6f} {nh:.6f}"


def collect_image_jobs(scenes: list[Path]) -> list[tuple[Path, list[str]]]:
    """Return list of (src_image_path, yolo_label_lines) for images that have at least one valid object."""
    jobs: list[tuple[Path, list[str]]] = []
    for scene in scenes:
        embedded = load_embedded(scene)
        for img in sorted(scene.iterdir()):
            if img.suffix.lower() not in (".jpg", ".jpeg", ".png"):
                continue
            ann = load_sidecar(img) or embedded.get(img.name)
            if not isinstance(ann, dict):
                continue
            objects = ann.get("objects") or []
            if not objects:
                continue
            w = ann.get("image_width")
            h = ann.get("image_height")
            if not (w and h):
                with Image.open(img) as im:
                    w, h = im.size
            lines = [ln for o in objects
                     if (ln := to_yolo_line(o.get("category"), o.get("bbox_xyxy"), w, h))]
            if lines:
                jobs.append((img, lines))
    return jobs


def main() -> None:
    args = parse_args()
    source: Path = args.source.resolve()
    output: Path = args.output.resolve()
    if not source.is_dir():
        sys.exit(f"[ERR] source not found: {source}")

    scenes = list(iter_scenes(source))
    if not scenes:
        sys.exit(f"[ERR] no scene folders under {source}")
    print(f"scenes: {len(scenes)}")

    rng = random.Random(args.seed)
    rng.shuffle(scenes)
    n_val = max(1, int(round(len(scenes) * args.val_ratio)))
    splits = {"val": scenes[:n_val], "train": scenes[n_val:]}
    print(f"train: {len(splits['train'])}  val: {len(splits['val'])}")

    for split, split_scenes in splits.items():
        img_dir = output / "images" / split
        lbl_dir = output / "labels" / split
        img_dir.mkdir(parents=True, exist_ok=True)
        lbl_dir.mkdir(parents=True, exist_ok=True)

        jobs = collect_image_jobs(split_scenes)
        print(f"\n[{split}] annotated images: {len(jobs)}")
        for src, lines in tqdm(jobs, desc=f"  copy {split}"):
            new_name = f"{src.parent.name}__{src.stem}"
            dst_img = img_dir / f"{new_name}{src.suffix.lower()}"
            dst_lbl = lbl_dir / f"{new_name}.txt"
            if not dst_img.exists():
                shutil.copy2(src, dst_img)
            dst_lbl.write_text("\n".join(lines) + "\n", encoding="utf-8")

    print("\nDone.")


if __name__ == "__main__":
    main()