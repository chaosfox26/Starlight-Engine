"""Compatibility entry point; every native target now uses the root CMake build."""
from pathlib import Path
import subprocess, sys
root = Path(__file__).resolve().parents[2]
raise SystemExit(subprocess.call([sys.executable, str(root/'Build-Native.py'), *sys.argv[1:]]))
