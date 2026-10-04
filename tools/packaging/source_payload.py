"""Select source-build inputs without generated outputs or repository metadata."""
from pathlib import Path


def source_files(root):
    root = Path(root).resolve()
    folders = ('editor/cpp', 'editor/native', 'deep/src', 'deep/engine',
               'deep/Physics', 'deep/projects', 'tools', 'cmake',
               'deep/third_party/JoltPhysics/Jolt',
               'deep/third_party/JoltPhysics/Build')
    excluded = (root/'editor/native/build', root/'deep/Physics/build')
    suffixes = {'.cpp', '.hpp', '.h', '.inl', '.hlsl', '.c', '.cmake', '.py',
                '.inc', '.natvis', '.manifest', '.rc', '.star', '.gliffy'}
    selected = set()
    for folder in folders:
        base = root/folder
        if not base.is_dir():
            raise FileNotFoundError('Required source directory: ' + folder)
        for source in base.rglob('*'):
            relative = source.relative_to(base)
            if any(part in ('.git', '__pycache__', 'CMakeFiles') for part in relative.parts):
                continue
            if any(source.is_relative_to(cache) for cache in excluded):
                continue
            if source.is_file() and (source.suffix.lower() in suffixes or source.name == 'CMakeLists.txt'):
                if not source.resolve().is_relative_to(root):
                    raise ValueError('Source path escapes package root')
                selected.add(source)
    for name in ('CMakeLists.txt', 'deep/third_party/JoltPhysics/LICENSE'):
        source = root/name
        if not source.is_file():
            raise FileNotFoundError('Required source file: ' + name)
        selected.add(source)
    return sorted(selected)
