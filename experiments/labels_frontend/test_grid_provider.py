"""Runtime admission is isolated so the normal NumPy provider stays untouched."""
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.parametrize('scenario',['indices-dense','indices-sparse','ogrid','mgrid','complex','single-slice',
    'invalid','invalid-dimensions','alias-protocol','foreign-exceptions','foreign-numeric','brush','gc','audit-reject'])
def test_grid_provider_policy(scenario):
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())+os.pathsep+env.get('PYTHONPATH','')
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_child.py'),scenario],
        cwd=directory.parents[1],env=env,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    record=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{"scenario"')))
    assert record['scenario']==scenario and record['status']=='PASS'
