"""Keep defining imports isolated from the normal runtime-contract process."""
import json
import os
from pathlib import Path
import subprocess
import sys
from types import ModuleType
import gc
import weakref

import pytest
import _frontend_policy_probe as probe
from capture_imports import instrument, MARKER


@pytest.mark.parametrize('scenario',['all50','identical-before-validation','identical-adapter','identical-class',
    'missing-scope','duplicate-scope','closed-replay','factory-after-close','factory-after-scope-release'])
def test_actual_defining_execution_capture(scenario):
    directory=Path(__file__).resolve().parent
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    probe_directory=Path(env['NAPARIXX_FRONTEND_PROBE_DIR']).resolve()
    env['PYTHONPATH']=str(probe_directory)+os.pathsep+env.get('PYTHONPATH','')
    result=subprocess.run([sys.executable,'-B',str(directory/'producer_child.py'),scenario],
                          cwd=directory.parents[1],env=env,capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stdout+'\n'+result.stderr
    record=json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{"scenario"')))
    assert record==dict(scenario=scenario,captured_functions=50,scopes=6,status='PASS')


def fixture_scope(tmp_path,monkeypatch,source='def f():\n    return 7\n',qualname='f'):
    path=tmp_path/'source.py'
    path.write_text(source)
    body,rows=instrument(path,[(5,{'qualname':qualname,'occurrence':1})])
    module=ModuleType('_naparixx_producer_fixture')
    monkeypatch.setitem(sys.modules,module.__name__,module)
    scope=probe.prepare_producer_capture_probe(module,body,rows)
    return module,scope,body,rows


@pytest.mark.parametrize('violation',['foreign-namespace','unregistered-module','wrong-instruction','outside-frame'])
def test_declaration_frame_and_instruction_guards(violation,tmp_path,monkeypatch):
    module,scope,body,rows=fixture_scope(tmp_path,monkeypatch)
    if violation=='foreign-namespace':
        operation=lambda:exec(scope.code,dict(module.__dict__))
    elif violation=='unregistered-module':
        monkeypatch.delitem(sys.modules,module.__name__)
        operation=lambda:exec(scope.code,module.__dict__)
    elif violation=='wrong-instruction':
        row=list(rows[0])
        row[3:5]=[0,2]
        scope=probe.prepare_producer_capture_probe(module,body,(tuple(row),))
        operation=lambda:exec(scope.code,module.__dict__)
    else:
        marker=next(value for value in scope.code.co_consts if type(value).__name__=='_ProducerMarker')
        operation=lambda:marker(5)
    with pytest.raises(TypeError):
        operation()
    assert 'f' not in module.__dict__


@pytest.mark.parametrize('closed',[False,True])
def test_same_code_reexecution_cannot_recapture(closed,tmp_path,monkeypatch):
    module,scope,body,rows=fixture_scope(tmp_path,monkeypatch)
    exec(scope.code,module.__dict__)
    function=module.f
    if closed:
        scope.finish()
    with pytest.raises(TypeError):
        exec(scope.code,module.__dict__)
    assert module.f is function and module.f()==7


def test_incomplete_capture_can_finish_after_the_real_definition(tmp_path,monkeypatch):
    module,scope,body,rows=fixture_scope(tmp_path,monkeypatch)
    with pytest.raises(TypeError,match='incomplete'):
        scope.finish()
    exec(scope.code,module.__dict__)
    scope.finish()
    assert module.f()==7


def test_class_cell_is_filled_by_the_actual_build_class(tmp_path,monkeypatch):
    source='class C:\n    def f(self):\n        return __class__\n'
    module,scope,body,rows=fixture_scope(tmp_path,monkeypatch,source,'C.f')
    exec(scope.code,module.__dict__)
    scope.finish()
    assert module.C().f() is module.C
    assert module.C.f.__closure__[0].cell_contents is module.C


def test_scope_code_weak_marker_and_module_cycle_are_collectible(tmp_path,monkeypatch):
    module,scope,body,rows=fixture_scope(tmp_path,monkeypatch)
    exec(scope.code,module.__dict__)
    scope.finish()
    module.owner=scope
    references=weakref.ref(scope),weakref.ref(module),weakref.ref(module.f)
    assert gc.is_tracked(scope)
    monkeypatch.undo()
    del scope,module
    gc.collect()
    gc.collect()
    assert all(reference() is None for reference in references)
