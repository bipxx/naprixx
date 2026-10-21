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


def test_grid_comprehension_frame():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_comprehension_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['status']=='PASS'


def test_grid_indices_locals():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_indices_locals_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==(9 if sys.version_info[:2]==(3,12) else 11)


def test_grid_getitem_locals():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_getitem_locals_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['status']=='PASS'


def test_grid_temporary_lifetime():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_temporary_lifetime_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==11


def test_grid_native_line_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_line_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==12


def test_grid_native_opcode_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_opcode_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==(16 if sys.version_info>=(3,13) else 15)


def test_grid_getter_line_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_getter_line_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==(14 if sys.version_info>=(3,12) else 13)


def test_grid_fallback_trace_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_fallback_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==20


def test_grid_getter_opcode_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_getter_opcode_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==(17 if sys.version_info>=(3,12) else 16)
