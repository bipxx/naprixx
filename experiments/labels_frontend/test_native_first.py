"""Native declaration construction precedes source callbacks and first calls."""
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.parametrize('scenario',['all50','metadata','paint-generic','generator-return','release'])
def test_native_first_declarations(scenario):
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())+os.pathsep+env.get('PYTHONPATH','')
    result=subprocess.run([sys.executable,'-B',str(directory/'native_first_child.py'),scenario],
        cwd=directory.parents[1],env=env,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    record=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{"scenario"')))
    assert record==dict(scenario=scenario,status='PASS',original_body_calls=0,initial_functions=50)
