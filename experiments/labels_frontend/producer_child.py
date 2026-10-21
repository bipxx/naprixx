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
    if scenario=='all50':
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
