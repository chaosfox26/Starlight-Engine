"""Compatibility entry point for the canonical Starlight CMake/Ninja or tool workflow."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[2] / 'tools/build/cmake_shaders.py'), run_name="__main__")
