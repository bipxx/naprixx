"""Fresh native-declaration process: selected original bodies may never run."""
import gc
import inspect
import json
import sys
import weakref
from types import CodeType

from run_tests import deny_archive
sys.addaudithook(deny_archive)
import _frontend_policy_probe as probe
from capture_imports import import_captured
from native_declarations import prepare_table,tuples
from inspect_inputs import ROOT,load_inputs,walk
from freeze_transport import profile


def main():
    inputs=load_inputs()
    selected={(code.co_filename,code.co_qualname,code.co_firstlineno) for _,code,_ in inputs}
    def payload(code):
        return bool(code.co_consts and isinstance(code.co_consts[-1],tuple) and
                    code.co_consts[-1] and code.co_consts[-1][0]=='frontend-probe-v0')
    calls=[]
    def watch(frame,event,arg):
        if event=='call' and (frame.f_code.co_filename,frame.f_code.co_qualname,frame.f_code.co_firstlineno) in selected:
            assert payload(frame.f_code),'selected original Python body entered'
            calls.append(frame.f_code.co_qualname)
    created=[]
    violations=[]
    def creation(function,new_value):
        code=function.__code__
        if (code.co_filename,code.co_qualname,code.co_firstlineno) in selected:
            created.append(function)
            if not payload(code):violations.append(code.co_qualname)
    table=prepare_table(probe)
    probe.start_watcher_probe(creation,True)
    previous=sys.getprofile()
    sys.setprofile(watch)
    try:
        scopes=import_captured(probe,table)
        assert not violations and len({id(fn) for fn in created})==50,(
            violations,len(created),[fn.__code__.co_qualname for fn in created])
        backend=ROOT/'src/naparixx/_backend.py'
        wrapper=next(code for code in walk(compile(backend.read_bytes(),str(backend),'exec',dont_inherit=True))
                     if code.co_qualname=='dispatch.<locals>.decorate.<locals>.call')
        functions=probe.validate_captured_frontend_probe(scopes,tuples(profile(wrapper)))
        assert len(functions)==50 and all(payload(fn.__code__) for fn in functions)
        assert set(functions)==set(created)
        scenario=sys.argv[1]
        if scenario=='metadata':
            for entry,function in enumerate(functions):
                source=inputs[entry][1]
                expected=source.co_consts[0] if source.co_consts and isinstance(source.co_consts[0],str) else None
                if entry in (36,38):
                    generic=inputs[31 if entry==36 else 33][1]
                    expected=generic.co_consts[0] if isinstance(generic.co_consts[0],str) else None
                assert function.__doc__==expected,(entry,function.__doc__,expected)
                assert function.__code__.co_freevars==source.co_freevars
                assert inspect.isgeneratorfunction(function)==bool(source.co_flags&0x20)
        elif scenario=='paint-generic':
            import numpy as np
            from napari.layers import Labels
            from naparixx import disable,enable
            from napari.layers.labels import _labels_utils as utils
            labels=Labels(np.zeros((6,6),dtype=np.int16))
            labels.brush_size=2
            labels.paint((2,3),9)
            assert np.count_nonzero(labels.data)>0 and labels.data.max()==9
            try:
                disable()
                assert utils.interpolate_coordinates(None,(0.,0.),1).shape==(1,2)
            finally:
                enable()
        elif scenario=='generator-return':
            from types import SimpleNamespace
            # The frozen-cursor branch must end with None, never
            # the docstring inserted into this genuine generator's constants.
            gen=functions[26](SimpleNamespace(cursor='circle_frozen'),SimpleNamespace(type='mouse_move'))
            try:next(gen)
            except StopIteration as error:assert error.value is None
            else:raise AssertionError('non-press draw unexpectedly yielded')
        elif scenario=='release':
            refs=tuple(weakref.ref(scope) for scope in scopes)+(weakref.ref(table),)
            del scopes,table
            gc.collect();gc.collect()
            assert all(reference() is None for reference in refs)
            from naparixx import _adapters
            generic=functions[31]
            child=_adapters.interpolation_adapter(generic)
            assert child.__wrapped__ is generic and payload(child.__code__)
            assert child(None,(0.,0.),1).shape==(1,2)
        elif scenario!='all50':
            raise AssertionError(scenario)
        assert not violations
    finally:
        sys.setprofile(previous)
        probe.stop_watcher_probe()
    print(json.dumps(dict(scenario=scenario,status='PASS',original_body_calls=0,initial_functions=50)))


if __name__=='__main__':main()
