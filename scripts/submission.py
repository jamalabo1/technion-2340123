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
    ROOT / "test_input1.txt",
    ROOT / "test_expected_output1.txt",
    ROOT / "submitters.txt"
]


def makefile_with_inlined_env() -> str:
    makefile = ROOT / "Makefile"
    env_file = ROOT / ".env"

    env_lines = env_file.read_text().splitlines() if env_file.exists() else []
    inline_block = [
        "# Values inlined from local .env by scripts/submission.py.",
        "# .env is intentionally not copied into the submission folder.",
        *env_lines,
    ]

    rendered_lines = []
    replaced_include = False
    for line in makefile.read_text().splitlines():
        if line.strip() == "include .env":
            rendered_lines.extend(inline_block)
            replaced_include = True
        else:
            rendered_lines.append(line)

    if not replaced_include:
        rendered_lines = inline_block + [""] + rendered_lines

    return "\n".join(rendered_lines) + "\n"


def main() -> None:
    OUT.mkdir(exist_ok=True)

    for old in OUT.iterdir():
        if old.is_file():
            old.unlink()

    for src in FILES:
        dst_name = src.name
        shutil.copy2(src, OUT / dst_name)

    (OUT / "Makefile").write_text(makefile_with_inlined_env())

    print(f"Created flat submission in {OUT}")


if __name__ == "__main__":
    main()
