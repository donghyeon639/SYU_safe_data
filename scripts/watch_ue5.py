"""
UE5 출력 폴더 실시간 감시 → 새 데이터 감지 시 convert_unreal_to_yolo.py 자동 실행.

사용법:
  python scripts/watch_ue5.py --source D:\exP12\Build\Demo\Windows\exP12\Saved
  python scripts/watch_ue5.py --source D:\exP12\Saved --source D:\exP12_2\Saved
"""

import argparse
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler
except ImportError:
    print("[오류] watchdog 미설치. pip install watchdog")
    raise SystemExit(1)

LEARN_DIR = Path(__file__).parent.parent
OUTPUT_DIR = LEARN_DIR / "datasets" / "safety"
CONVERT_SCRIPT = Path(__file__).parent / "convert_unreal_to_yolo.py"

TRIGGER_COUNT = 12  # 사고 시나리오 1건 = 이미지 12장


class UE5Handler(FileSystemEventHandler):
    def __init__(self, source: Path) -> None:
        self.source = source
        self.new_files: list[str] = []
        self.last_run = time.time()
        self._lock = threading.Lock()

    def on_created(self, event) -> None:
        if event.is_directory:
            return
        p = Path(event.src_path)
        if p.suffix.lower() not in (".jpg", ".jpeg", ".png"):
            return

        should_run = False
        count = 0
        with self._lock:
            self.new_files.append(event.src_path)
            print(f"  [새 파일] {p.name}  (누적: {len(self.new_files)}장)")
            elapsed = time.time() - self.last_run
            if len(self.new_files) >= TRIGGER_COUNT or (self.new_files and elapsed > 60):
                count = len(self.new_files)
                self.new_files.clear()
                self.last_run = time.time()
                should_run = True

        if should_run:
            self._run_convert(count)

    def _run_convert(self, count: int) -> None:
        print(f"\n[변환 시작] {count}장 감지 → convert_unreal_to_yolo.py 실행...")
        subprocess.run([
            sys.executable, str(CONVERT_SCRIPT),
            "--source", str(self.source),
            "--output", str(OUTPUT_DIR),
        ])
        print("[변환 완료] 계속 감시 중...\n")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--source", type=Path, action="append", required=True,
                   help="감시할 UE5 Saved 경로 (여러 개 가능: --source 경로1 --source 경로2)")
    args = p.parse_args()

    valid = [path for path in args.source if path.exists()]
    if not valid:
        print("[오류] 감시할 폴더가 없습니다. --source 경로를 확인하세요.")
        raise SystemExit(1)

    observer = Observer()
    for path in valid:
        observer.schedule(UE5Handler(path), str(path), recursive=True)
        print(f"감시 중: {path}")

    print(f"이미지 {TRIGGER_COUNT}장 쌓이면 자동 변환 실행")
    print("종료: Ctrl+C\n")

    observer.start()
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()
        print("\n감시 종료")
    observer.join()


if __name__ == "__main__":
    main()
