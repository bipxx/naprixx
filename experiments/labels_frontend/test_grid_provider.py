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


def test_grid_retained_traced_frames():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_retained_frame_child.py'),'--traced-only'],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout)['controls']==16


def test_grid_retained_frames_completion_transport():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_retained_frame_child.py'),'--completion-transport'],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout)['controls']==24


@pytest.mark.parametrize('ambient',[False,True])
def test_grid_completion_transport_direct_errors(ambient):
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    command=[sys.executable,'-B',str(directory/'grid_retained_frame_child.py'),'--completion-transport','--direct-call']
    if ambient:command.append('--ambient')
    result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    assert json.loads(result.stdout)['controls']==8


def test_grid_trace_callback_reentry():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_reentry_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    data=json.loads(result.stdout)
    assert data['controls']==64 and data['nested_prepared_code_release_checked']


def test_grid_active_completion_carrier_gc():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_carrier_gc_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    data=json.loads(result.stdout)
    assert data['controls']==9 and not data['profile_event_parity_claimed']
    assert sum(row['weak_result_release_checks'] for row in data['cases'])==4


def test_grid_completion_carrier_cycles_and_profile_unwind():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_carrier_unwind_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    data=json.loads(result.stdout)
    assert data['controls']==18
    assert sum(row['native_cycle_release_checks'] for row in data['cases'])==18
    assert sum(row['source_cycle_release_checks'] for row in data['cases'])==12
    assert all(row['values_and_nested_outcome_equal'] and row['caller_handled_exception_restored'] for row in data['cases'])


@pytest.mark.parametrize('source_without_hook',[False,True])
def test_grid_explicit_evaluator_scope(source_without_hook):
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    command=[sys.executable,'-B',str(directory/'grid_eval_scope_child.py')]
    if source_without_hook:command.append('--source-without-hook')
    result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=30)
    expected_gap=source_without_hook and sys.version_info<(3,13)
    assert result.returncode==int(expected_gap),result.stdout+'\n'+result.stderr
    data=json.loads(result.stdout)
    assert data['controls']==24 and data['duplicate_enable_active_disable_and_duplicate_disable_rejected']
    assert all(row['policy_values_equal'] for row in data['cases'])
    assert sum(not row['events_equal'] for row in data['cases'])==(4 if expected_gap else 0)
    assert sum(not row['retained_equal'] for row in data['cases'])==(3 if expected_gap else 0)
    assert sum(not row['errors_equal'] for row in data['cases'])==(3 if expected_gap else 0)


def test_grid_per_function_scope():
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    env['PYTHONPATH']=str(Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve())
    result=subprocess.run([sys.executable,'-B',str(directory/'grid_function_scope_child.py')],
        env=env,capture_output=True,text=True,timeout=30)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    data=json.loads(result.stdout)
    if sys.version_info<(3,12):
        assert data['status']=='OPEN_CP311_FUNCTION_CALL_BYPASS' and data['controls']==0
    else:
        assert data['controls']==24 and data['unhooked_original_source_oracle']
        assert data['unselected_source_positions_unchanged'] and data['enabled_cyclic_function_and_code_collected']
        assert all(all(row[k] for k in ('values_equal','events_equal','retained_equal','errors_equal')) for row in data['cases'])
