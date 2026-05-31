"""
UE5 출력 폴더 실시간 감시 → 새 데이터 감지 시 변환 + 주기적 자동 학습.

사용법:
  python scripts/watch_ue5.py --source D:\Windows\ExP12\Saved
  python scripts/watch_ue5.py --source D:\Windows\ExP12\Saved --train-every 20
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

TRIGGER_COUNT = 12   # 사고 시나리오 1건 = 이미지 12장


class TrainingManager:
    """학습을 백그라운드 스레드에서 관리. 동시에 두 번 실행되지 않도록 보호."""

    def __init__(self, train_every: int) -> None:
        self.train_every = train_every      # N 사고마다 학습 트리거
        self._accident_count = 0
        self._lock = threading.Lock()
        self._training = False

    def on_accident_converted(self) -> None:
        with self._lock:
            self._accident_count += 1
            count = self._accident_count
            should_train = (count % self.train_every == 0) and not self._training

        if should_train:
            with self._lock:
                self._training = True
            threading.Thread(target=self._run_training, daemon=True).start()

    def _run_training(self) -> None:
        try:
            self._do_train()
        finally:
            with self._lock:
                self._training = False

    def _do_train(self) -> None:
        venv_python = LEARN_DIR / ".venv" / "Scripts" / "python.exe"
        python = str(venv_python) if venv_python.exists() else sys.executable

        # 이전 학습 결과(last.pt)가 있으면 이어서 학습, 없으면 사전학습 가중치로 시작
        last_pt = LEARN_DIR / "output" / "yolo11m_safety" / "weights" / "last.pt"
        model = str(last_pt) if last_pt.exists() else "yolo11m.pt"
        epochs = 30 if last_pt.exists() else 60

        print(f"\n[학습 시작] model={Path(model).name}  epochs={epochs}")
        cmd = [
            python, "-m", "ultralytics",
            "detect", "train",
            f"model={model}",
            f"data={LEARN_DIR / 'data.yaml'}",
            f"epochs={epochs}",
            "imgsz=640",
            "batch=4",
            f"project={LEARN_DIR / 'output'}",
            "name=yolo11m_safety",
            "exist_ok=True",
        ]
        result = subprocess.run(cmd, cwd=str(LEARN_DIR))
        if result.returncode == 0:
            print("[학습 완료] 계속 감시 중...\n")
        else:
            print(f"[학습 오류] returncode={result.returncode}\n")


class UE5Handler(FileSystemEventHandler):
    def __init__(self, source: Path, trainer: TrainingManager) -> None:
        self.source = source
        self.trainer = trainer
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
        result = subprocess.run([
            sys.executable, str(CONVERT_SCRIPT),
            "--source", str(self.source),
            "--output", str(OUTPUT_DIR),
        ])
        print("[변환 완료] 계속 감시 중...\n")
        if result.returncode == 0:
            self.trainer.on_accident_converted()


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--source", type=Path, action="append", required=True,
                   help="감시할 UE5 Saved 경로 (여러 개 가능)")
    p.add_argument("--train-every", type=int, default=20,
                   help="N 사고 시나리오마다 학습 트리거 (기본: 20)")
    args = p.parse_args()

    valid = [path for path in args.source if path.exists()]
    if not valid:
        print("[오류] 감시할 폴더가 없습니다. --source 경로를 확인하세요.")
        raise SystemExit(1)

    trainer = TrainingManager(train_every=args.train_every)

    observer = Observer()
    for path in valid:
        observer.schedule(UE5Handler(path, trainer), str(path), recursive=True)
        print(f"감시 중: {path}")

    print(f"이미지 {TRIGGER_COUNT}장 쌓이면 자동 변환")
    print(f"사고 시나리오 {args.train_every}건마다 자동 학습 (학습 중에도 감시 계속)")
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
