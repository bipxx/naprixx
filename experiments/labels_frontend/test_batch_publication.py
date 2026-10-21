"""Keep irrevocable audit hooks and interpreter watchers in child processes."""
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.parametrize('scenario',['success-cache','audit-observe','audit-reject','audit-reentry',
    'audit-other-thread','watcher-observe','watcher-error','watcher-mutate-default','foreign-code-change','duplicate'])
def test_batch_publication_protocol(scenario):
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())+os.pathsep+env.get('PYTHONPATH','')
    result=subprocess.run([sys.executable,'-B',str(directory/'batch_child.py'),scenario],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    record=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{"scenario"')))
    assert record==dict(scenario=scenario,status='PASS')
