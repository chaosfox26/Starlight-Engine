"""Starter cards (*.starscene) are pinned to LF by a tracked .gitattributes, so every clone checks them out byte for byte (DEP-02)."""
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def git(*args):
    return subprocess.run(['git', *args], capture_output=True, text=True, cwd=str(root), check=True).stdout.strip()


assert (root / '.gitattributes').is_file() and git('ls-files', '--', '.gitattributes') == '.gitattributes', '.gitattributes is not tracked, so a fresh clone would not get the LF rule'
probe = 'probe.starscene'
attribute = git('check-attr', 'eol', '--', probe)
assert attribute == probe + ': eol: lf', 'starter cards are not pinned to LF: ' + attribute
print('ok: starter cards check out with LF line endings from any clone')
