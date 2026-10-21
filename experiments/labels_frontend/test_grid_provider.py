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
    assert json.loads(result.stdout.splitlines()[0])['controls']==22


def test_grid_getter_opcode_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_getter_opcode_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==(17 if sys.version_info>=(3,12) else 16)


def test_grid_fallback_opcode_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_fallback_opcode_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==22


def test_grid_observer_errors():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_observer_error_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==14


def test_grid_provider_positions_with_opcode_instrumentation():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_provider_traced_position_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['controls']==16


def test_grid_positions_recycled_frames_without_tracer():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_position_reuse_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout.splitlines()[0])['native_calls']==100


# This owner implements 3.12's cache/version rules. The strict cross-minor
# diagnostics separately retain 3.11's unimplemented quickening behavior.
if sys.version_info[:2]==(3,12):
    def test_grid_python312_adaptive_invalidation():
        directory=Path(__file__).resolve().parent
        env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
        env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
        result=subprocess.run([sys.executable,'-B',str(directory/'grid_adaptive_mutation_child.py')],
            env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stdout+'\n'+result.stderr
        assert json.loads(result.stdout)['controls']==3

    def test_grid_python312_adaptive_code_lifetime():
        directory=Path(__file__).resolve().parent
        env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
        env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
        result=subprocess.run([sys.executable,'-B',str(directory/'grid_adaptive_lifetime_child.py')],
            env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stdout+'\n'+result.stderr
        assert json.loads(result.stdout)['controls']==5

    def test_grid_python312_hot_handoff():
        directory=Path(__file__).resolve().parent
        env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
        env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
        result=subprocess.run([sys.executable,'-B',str(directory/'grid_hot_handoff_child.py')],
            env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stdout+'\n'+result.stderr
        assert json.loads(result.stdout)['controls']==7

    def test_grid_python312_property_positions_and_errors():
        directory=Path(__file__).resolve().parent
        env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
        env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
        result=subprocess.run([sys.executable,'-B',str(directory/'grid_property_position_child.py')],
            env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stdout+'\n'+result.stderr
        assert json.loads(result.stdout)['controls']==8

    @pytest.mark.parametrize('option',['--lines','--opcodes','--toggle','--warm-opcodes','--skip-attributes',
        '--inside-disable','--inside-local-off','--inside-enable','--inside-replace'])
    def test_grid_python312_property_instrumentation_transitions(option):
        directory=Path(__file__).resolve().parent
        env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
        env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
        result=subprocess.run([sys.executable,'-B',str(directory/'grid_property_position_child.py'),option],
            env=env,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,result.stdout+'\n'+result.stderr
        data=json.loads(result.stdout)
        assert data['controls']==8
        assert all(row['trace_equal'] and row['source_trace_sha256']==row['native_trace_sha256'] for row in data['cases'])
