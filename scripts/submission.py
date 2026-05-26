from pathlib import Path
import shutil

# scripts/submission.py
ROOT = Path(__file__).resolve().parents[1]

# submission folder
OUT = ROOT / "submission"

# what to include
FILES = [
    *Path(ROOT / "include").glob("**/*.h"),
    *Path(ROOT / "impl").glob("**/*.h"),
    *Path(ROOT / "impl").glob("**/*.cpp"),
    *Path(ROOT).glob("*.cpp"),
    *Path(ROOT).glob("*.h"),
    ROOT / ".env",
    ROOT / "Makefile",
    ROOT / "test_input1.txt",
    ROOT / "test_expected_output1.txt",
    ROOT / "submitters.txt"
]

OUT.mkdir(exist_ok=True)

for old in OUT.iterdir():
    if old.is_file():
        old.unlink()

for src in FILES:
    dst_name = src.name
    shutil.copy2(src, OUT / dst_name)

print(f"Created flat submission in {OUT}")
