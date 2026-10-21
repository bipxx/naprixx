"""Fresh-process actual import oracle for declaration capture."""
import gc
import inspect
import json
from pathlib import Path
import sys
from types import FunctionType
import weakref

from run_tests import deny_archive
sys.addaudithook(deny_archive)
import _frontend_policy_probe as probe
from capture_imports import import_captured
from freeze_transport import profile
from inspect_inputs import ROOT,walk


def main():
    scopes=import_captured(probe)
    backend=ROOT/'src/naparixx/_backend.py'
    wrapper=next(code for code in walk(compile(backend.read_bytes(),str(backend),'exec',dont_inherit=True))
                 if code.co_qualname=='dispatch.<locals>.decorate.<locals>.call')
    def tuples(value):
        return tuple(tuples(item) for item in value) if isinstance(value,list) else value
    wrapper_profile=tuples(profile(wrapper))
    def validate(values=scopes):
        return probe.validate_captured_frontend_probe(values,wrapper_profile)
    functions=validate()
    assert len(functions)==50 and len({id(function) for function in functions})==50
    scenario=sys.argv[1]
    if scenario.startswith(('prepare-','publish-')):
        from compiler_controls import controls
        from freeze_transport import shells
        from inspect_inputs import STAGE
        frozen=controls()
        source_profiles=tuples([row['original_profile_18'] for row in frozen['frontend_rows']])
        shell_profiles=tuples([row['shell_profile_18'] for row in frozen['frontend_rows']])
        expression_profiles=tuples([row['shell_profile_18'] for row in frozen['expression_rows']])
        frontend_shells=tuple(shells(STAGE/'frontend_shells.py','f',50))
        expression_shells=tuple(shells(STAGE/'expression_shells.py','e',7))
        before=tuple(function.__code__ for function in functions)
        def prepare(profiles=source_profiles):
            return probe.prepare_captured_frontend_probe(scopes,frontend_shells,profiles,
                shell_profiles,expression_shells,expression_profiles,wrapper_profile)
        if scenario=='prepare-profile-rejection':
            altered=list(source_profiles)
            row=list(altered[49]);row[14]=('INT',-1);altered[49]=tuple(row)
            try:
                prepare(tuple(altered))
            except TypeError:
                pass
            else:
                raise AssertionError('altered original profile accepted')
        elif scenario=='prepare-audit-mutation':
            from napari.layers import Labels
            fired=[]
            def mutate(event,args):
                if event=='code.__new__' and not fired:
                    fired.append(True)
                    old=Labels.paint
                    Labels.paint=FunctionType(old.__code__,old.__globals__,old.__name__,old.__defaults__,old.__closure__)
            sys.addaudithook(mutate)
            try:
                prepare()
            except TypeError:
                assert fired
            else:
                raise AssertionError('reentrant code-construction mutation accepted')
        else:
            plan=prepare()
            plan.validate()
            assert all(left is right for left,right in zip(plan.functions,functions,strict=True))
            codes=plan.prepared_codes
            assert len(codes)==50 and all(code is not old for code,old in zip(codes,before,strict=True))
            if scenario.startswith('publish-'):
                from napari.layers import Labels
                metadata=tuple((fn.__globals__,fn.__builtins__,fn.__closure__,fn.__defaults__,fn.__kwdefaults__)
                               for fn in functions)
                observed=[]
                scope_refs=tuple(weakref.ref(scope) for scope in scopes)
                def all_old():
                    return all(fn.__code__ is code for fn,code in zip(functions,before,strict=True))
                foreign_key=None
                key_calls=[]
                if scenario=='publish-foreign-key':
                    import sys as _sys
                    dictionary=_sys.modules['napari.layers.labels.labels'].__dict__
                    class Key(str):
                        __hash__=str.__hash__
                        def __eq__(self,other):
                            key_calls.append(other)
                            return str.__eq__(self,other)
                    foreign_key=Key('Labels')
                    del dictionary['Labels']
                    dictionary[foreign_key]=Labels
                    key_calls.clear()
                if scenario in ('publish-audit-reject','publish-reentry','publish-audit-mutate'):
                    def audit(event,args):
                        if event!='object.__setattr__' or args[1]!='__code__' or args[0] not in functions:
                            return
                        observed.append(args[0]);assert all_old()
                        if scenario=='publish-audit-reject' and len(observed)==50:
                            raise ValueError('reject final publication notification')
                        if scenario=='publish-audit-mutate' and len(observed)==1:
                            old=Labels.paint
                            Labels.paint=FunctionType(old.__code__,old.__globals__,old.__name__,old.__defaults__,old.__closure__)
                        if scenario=='publish-reentry':
                            try: plan.publish_experiment()
                            except RuntimeError as error: assert 'already active' in str(error)
                            else: raise AssertionError('reentrant plan publication accepted')
                    sys.addaudithook(audit)
                if scenario=='publish-watcher':
                    def watcher(fn,code):
                        if fn in functions:
                            observed.append(fn);assert all_old()
                    probe.start_watcher_probe(watcher)
                if scenario in ('publish-audit-reject','publish-audit-mutate','publish-foreign-key'):
                    try: plan.publish_experiment()
                    except (ValueError,TypeError): pass
                    else: raise AssertionError('publication accepted a rejected/drifted cohort')
                    assert not plan.published and all_old()
                    if foreign_key is not None:
                        assert key_calls==[]
                        del dictionary[foreign_key]
                        dictionary['Labels']=Labels
                else:
                    import numpy as np
                    if scenario=='publish-all50':
                        oracle=Labels(np.zeros((6,6),dtype=np.int16))
                        oracle.brush_size=2
                        oracle.paint((2,3),9)
                        expected=oracle.data.copy()
                    plan.publish_experiment()
                    assert plan.published
                    plan.validate()
                    assert all(fn.__code__ is code for fn,code in zip(functions,codes,strict=True))
                    after=tuple((fn.__globals__,fn.__builtins__,fn.__closure__,fn.__defaults__,fn.__kwdefaults__)
                                for fn in functions)
                    assert all(left is right for old,new in zip(metadata,after,strict=True)
                               for left,right in zip(old,new,strict=True))
                    if scenario=='publish-watcher':
                        probe.stop_watcher_probe();assert len(observed)==50
                    if scenario=='publish-reentry': assert len(observed)==50
                    try: plan.publish_experiment()
                    except RuntimeError: pass
                    else: raise AssertionError('plan published twice')
                    if scenario=='publish-all50':
                        actual=Labels(np.zeros((6,6),dtype=np.int16))
                        actual.brush_size=2
                        actual.paint((2,3),9)
                        np.testing.assert_array_equal(actual.data,expected)
                        from naparixx import disable,enable
                        from napari.layers.labels import _labels_utils as utils
                        try:
                            disable()
                            assert utils.interpolate_coordinates(None,(0.,0.),1).shape==(1,2)
                        finally:
                            enable()
                    if scenario=='publish-release':
                        plan_ref=weakref.ref(plan)
                        del prepare,validate,scopes,plan
                        gc.collect();gc.collect()
                        assert plan_ref() is None and all(reference() is None for reference in scope_refs)
                        assert functions[16](type('L',(),{'_contiguous':True})()) is True
            elif scenario=='prepare-runtime':
                from types import SimpleNamespace
                def native(entry):
                    original=functions[entry]
                    clone=FunctionType(codes[entry],original.__globals__,original.__name__,original.__defaults__,original.__closure__)
                    clone.__kwdefaults__=original.__kwdefaults__
                    return clone
                for entry,field in ((16,'_contiguous'),(18,'_brush_size'),(21,'_selected_label'),(40,'_ndim')):
                    obj=SimpleNamespace(**{field:3})
                    assert native(entry)(obj)==functions[entry](obj)
                import numpy as np
                args=((3,4),1.5,np.array([1,2]),np.array([1,2]))
                np.testing.assert_array_equal(native(10)(*args),functions[10](*args))
                assert native(39)(np.zeros(3)) is functions[39](np.zeros(3))
                generic=inspect.unwrap(__import__('napari.layers.labels._labels_utils',fromlist=['interpolate_coordinates']).interpolate_coordinates)
                adapter=native(35)(generic)
                assert adapter.__wrapped__ is generic
                assert adapter(None,(0.,0.),1).shape==(1,2)
            elif scenario=='prepare-plan-release':
                scope_refs=tuple(weakref.ref(scope) for scope in scopes)
                plan_ref=weakref.ref(plan)
                # prepare and validate both have references to the scope tuple.
                del prepare,validate,scopes,plan
                gc.collect();gc.collect()
                assert plan_ref() is None and all(reference() is None for reference in scope_refs)
                # The prepared CodeTypes are owner-free after their plan dies.
                assert FunctionType(codes[16],functions[16].__globals__)(type('L',(),{'_contiguous':True})()) is True
            elif scenario!='prepare-all50':
                raise AssertionError(scenario)
        if not scenario.startswith('publish-'):
            assert all(function.__code__ is code for function,code in zip(functions,before,strict=True))
    elif scenario=='all50':
        assert all(inspect.isfunction(function) for function in functions)
        # These instrumented first-import controls are Python oracles, not the
        # final native application. An existing simple public path still works.
        assert functions[16](type('Layer',(),{'_contiguous':True})()) is True
    elif scenario=='identical-before-validation':
        from napari.layers import Labels
        source=Labels.paint
        clone=FunctionType(source.__code__,source.__globals__,source.__name__,source.__defaults__,source.__closure__)
        Labels.paint=clone
        try:
            validate()
        except TypeError as error:
            assert 'captured defining execution' in str(error)
        else:
            raise AssertionError('identical pre-validation replacement accepted')
    elif scenario=='identical-adapter':
        from napari.layers.labels import _labels_utils as utils
        wrapper=utils.interpolate_coordinates
        cells=dict(zip(wrapper.__code__.co_freevars,wrapper.__closure__,strict=True))
        source=cells['adapter'].cell_contents
        clone=FunctionType(source.__code__,source.__globals__,source.__name__,source.__defaults__,source.__closure__)
        clone.__dict__.update(source.__dict__)
        cells['adapter'].cell_contents=clone
        try:
            validate()
        except TypeError as error:
            assert 'captured defining execution' in str(error)
        else:
            raise AssertionError('identical adapter replacement accepted')
    elif scenario=='identical-class':
        from napari.layers.labels import _labels_mouse_bindings as mouse
        original=mouse.BrushSizeOnMouseMove
        clone=type('BrushSizeOnMouseMove',original.__bases__,dict(vars(original)))
        mouse.BrushSizeOnMouseMove=clone
        try:
            validate()
        except TypeError as error:
            assert 'class differs from its captured defining execution' in str(error)
        else:
            raise AssertionError('class replacement reusing actual original methods accepted')
    elif scenario in ('factory-after-close','factory-after-scope-release'):
        from naparixx import _adapters
        from napari.layers.labels import _labels_utils as utils
        original=inspect.unwrap(utils.interpolate_coordinates)
        refs=tuple(weakref.ref(scope) for scope in scopes)
        if scenario=='factory-after-scope-release':
            # The validator closure has a default reference to scopes as well.
            del validate,scopes
            gc.collect()
            gc.collect()
            assert all(reference() is None for reference in refs)
        adapter=_adapters.interpolation_adapter(original)
        assert adapter.__wrapped__ is original
        assert adapter.__name__==original.__name__
        assert adapter(None,(0.,0.),1).shape==(1,2)
        if scenario=='factory-after-close':
            assert validate()[36] is functions[36] and adapter is not functions[36]
    elif scenario=='missing-scope':
        try:
            validate(scopes[:-1])
        except TypeError as error:
            assert 'complete fifty' in str(error)
        else:
            raise AssertionError('partial producer set accepted')
    elif scenario=='duplicate-scope':
        try:
            validate(scopes+(scopes[0],))
        except TypeError as error:
            assert 'duplicate' in str(error)
        else:
            raise AssertionError('duplicate producer set accepted')
    elif scenario=='closed-replay':
        scope=scopes[0]
        try:
            scope.finish()
        except TypeError as error:
            assert 'already closed' in str(error)
        else:
            raise AssertionError('repeated close accepted')
    else:
        raise AssertionError(scenario)
    print(json.dumps(dict(scenario=scenario,captured_functions=len(functions),scopes=6,status='PASS')))


if __name__=='__main__':
    main()
