"""
UE5 AccidentScreenshots/ + NormalScreenshots/ → YOLO 포맷 변환.

입력 구조:
  <source>/AccidentScreenshots/<scene>/
    metadata.json   ← UE5 팀이 bbox 추가 예정
    t0.0s_N.jpg / E.jpg / S.jpg / W.jpg
    t0.5s_N/E/S/W.jpg
    t4.0s_N/E/S/W.jpg  (총 12장)

  <source>/NormalScreenshots/<scene>/
    metadata.json
    N.jpg / E.jpg / S.jpg / W.jpg  (총 4장)

metadata.json 에서 기대하는 bbox 형식 (UE5 팀과 협의 필요):
  {
    "images": {
      "t0.0s_N.jpg": {"bbox_min_x": 0.3, "bbox_min_y": 0.2,
                      "bbox_max_x": 0.6, "bbox_max_y": 0.8},
      ...
    }
  }
  bbox 좌표는 정규화 값(0~1). 해당 이미지에 worker 없으면 항목 생략 (배경으로 처리).

출력:
  datasets/safety/
    images/{train,val}/*.jpg
    labels/{train,val}/*.txt   worker bbox 또는 빈 파일(배경)

사용법:
  python scripts/convert_unreal_to_yolo.py --source D:\\exP12\\add_2\\Saved --output datasets\\safety
"""
from __future__ import annotations

import argparse
import json
import random
import shutil
import sys
from pathlib import Path

from tqdm import tqdm

CLASS_WORKER = 0


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--source", required=True, type=Path,
                   help="Saved 경로 (AccidentScreenshots/, NormalScreenshots/ 포함)")
    p.add_argument("--output", required=True, type=Path,
                   help="출력 루트 (예: datasets/safety)")
    p.add_argument("--val-ratio", type=float, default=0.1)
    p.add_argument("--seed", type=int, default=42)
    return p.parse_args()


def bbox_to_yolo(min_x: float, min_y: float, max_x: float, max_y: float) -> str | None:
    """정규화 bbox (min/max) → YOLO (class xc yc w h). 너무 작으면 None."""
    w = max_x - min_x
    h = max_y - min_y
    if w < 0.01 or h < 0.01:
        return None
    xc = max(0.0, min(1.0, (min_x + max_x) / 2))
    yc = max(0.0, min(1.0, (min_y + max_y) / 2))
    w  = max(0.0, min(1.0, w))
    h  = max(0.0, min(1.0, h))
    return f"{CLASS_WORKER} {xc:.6f} {yc:.6f} {w:.6f} {h:.6f}"


def load_bbox_map(meta_path: Path) -> dict[str, str]:
    """metadata.json → {파일명: YOLO 라벨 문자열} 매핑. bbox 없으면 빈 dict."""
    if not meta_path.is_file():
        return {}
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8-sig"))
    except Exception as e:
        print(f"  [경고] {meta_path} 파싱 실패: {e}", file=sys.stderr)
        return {}

    images = meta.get("images", {})
    if not isinstance(images, dict):
        # 현재 UE5 출력: images 가 파일명 목록(list)임 → bbox 없음, 배경으로 처리
        return {}

    result: dict[str, str] = {}
    for fname, info in images.items():
        label = bbox_to_yolo(
            info["bbox_min_x"], info["bbox_min_y"],
            info["bbox_max_x"], info["bbox_max_y"],
        )
        if label:
            result[fname] = label
    return result


def collect_jobs(scenes_dir: Path) -> list[tuple[Path, str]]:
    """씬 폴더들을 순회 → (이미지 경로, YOLO 라벨) 목록."""
    jobs: list[tuple[Path, str]] = []
    if not scenes_dir.is_dir():
        return jobs

    for scene in sorted(scenes_dir.iterdir()):
        if not scene.is_dir():
            continue
        bbox_map = load_bbox_map(scene / "metadata.json")
        for img in sorted(scene.glob("*.jpg")):
            label = bbox_map.get(img.name, "")
            jobs.append((img, label))

    return jobs


def write_split(jobs: list[tuple[Path, str]], img_dir: Path, lbl_dir: Path) -> None:
    img_dir.mkdir(parents=True, exist_ok=True)
    lbl_dir.mkdir(parents=True, exist_ok=True)
    for img_path, label in tqdm(jobs, desc=f"  {img_dir.parts[-2]}/{img_dir.name}"):
        stem = f"{img_path.parent.name}__{img_path.stem}"
        dst_img = img_dir / f"{stem}.jpg"
        dst_lbl = lbl_dir / f"{stem}.txt"
        if not dst_img.exists():
            shutil.copy2(img_path, dst_img)
        dst_lbl.write_text(label + "\n" if label else "", encoding="utf-8")


def main() -> None:
    args = parse_args()
    source = args.source.resolve()
    output = args.output.resolve()

    if not source.is_dir():
        sys.exit(f"[ERR] source 없음: {source}")

    acc_jobs  = collect_jobs(source / "AccidentScreenshots")
    norm_jobs = collect_jobs(source / "NormalScreenshots")
    all_jobs  = acc_jobs + norm_jobs

    if not all_jobs:
        sys.exit("[ERR] 이미지를 찾을 수 없습니다.")

    labeled = sum(1 for _, lbl in all_jobs if lbl)
    print(f"사고 이미지  : {len(acc_jobs)}장")
    print(f"정상 이미지  : {len(norm_jobs)}장  (배경)")
    print(f"bbox 있는 이미지 : {labeled}장 / {len(all_jobs)}장")

    if labeled == 0:
        print("\n[경고] bbox 데이터가 없습니다. UE5 팀의 어노테이션 작업 완료 후 다시 실행하세요.")

    rng = random.Random(args.seed)
    rng.shuffle(all_jobs)
    n_val = max(1, int(round(len(all_jobs) * args.val_ratio)))
    splits = {"val": all_jobs[:n_val], "train": all_jobs[n_val:]}

    for split, jobs in splits.items():
        write_split(jobs, output / "images" / split, output / "labels" / split)
        print(f"  {split}: {len(jobs)}장")

    print("\n변환 완료.")


if __name__ == "__main__":
    main()
