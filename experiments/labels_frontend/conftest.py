"""Load only an explicitly built experimental extension."""
import os
from pathlib import Path
import sys

directory=os.environ.get('NAPARIXX_FRONTEND_PROBE_DIR')
if not directory:
    raise RuntimeError('Set NAPARIXX_FRONTEND_PROBE_DIR to the standalone experiment build output')
sys.path.insert(0,str(Path(__file__).resolve().parent))
sys.path.insert(0,str(Path(directory).resolve(strict=True)))
