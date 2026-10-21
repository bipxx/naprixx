"""Actual module/class/decorator binding checks; no production code handoff."""
import gc
import inspect
from pathlib import Path
import sys
from types import FunctionType
import weakref

import pytest
import _frontend_policy_probe as probe

ROOT=Path(__file__).resolve().parents[2]
BASE=Path(__file__).resolve().parent
sys.path.append(str(BASE))
import reference_bindings as shared
from freeze_transport import profile, shells
from inspect_inputs import walk
from napari.layers.labels import labels as labels_module
from naparixx import _backend

EXPRESSION_SHELLS=tuple(shells(BASE/'expression_shells.py','e',7))
EXPRESSION_PROFILES=tuple(shared.tuples(row['shell_profile_18']) for row in shared.FROZEN['expression_rows'])
wrapper_codes=list(walk(compile((ROOT/'src/naparixx/_backend.py').read_bytes(),str(ROOT/'src/naparixx/_backend.py'),'exec',dont_inherit=True)))
WRAPPER_PROFILE=shared.tuples(profile(next(code for code in wrapper_codes if code.co_qualname=='dispatch.<locals>.decorate.<locals>.call')))


def prepare():
    return probe.prepare_canonical_snapshot_probe(shared.SHELLS,shared.SOURCE_PROFILES,
        shared.SHELL_PROFILES,EXPRESSION_SHELLS,EXPRESSION_PROFILES,WRAPPER_PROFILE)


@pytest.mark.parametrize('entry',range(50))
def test_resolves_exact_canonical_function_identity(entry):
    snapshot=prepare()
    assert snapshot.functions[entry] is shared.ORIGINALS[entry]
    assert snapshot.prepared_codes[entry] is not shared.ORIGINALS[entry].__code__
    assert snapshot.prepared_codes[entry].co_name==shared.ORIGINALS[entry].__code__.co_name


def test_preparation_does_not_modify_functions_or_aliases():
    before=tuple(function.__code__ for function in shared.ORIGINALS)
    snapshot=prepare()
    snapshot.validate()
    assert all(function.__code__ is code for function,code in zip(shared.ORIGINALS,before,strict=True))
    assert len(snapshot.functions)==len(snapshot.prepared_codes)==50


@pytest.mark.parametrize('target',[
    'class-method','staticmethod-descriptor','property-descriptor','module-function',
    'registered-module','defining-class','wrapper-binding','error-class-cell',
    'wrapper-original-cell','adapter-original-cell','wrapper-wrapped-alias',
    'adapter-wrapped-alias','wrapper-code','function-defaults','function-code',
])
def test_snapshot_rejects_binding_changes_without_mutation(target,monkeypatch):
    snapshot=prepare()
    before=tuple(function.__code__ for function in shared.ORIGINALS)
    wrapper=shared.utils.interpolate_coordinates
    wrapper_cells=dict(zip(wrapper.__code__.co_freevars,wrapper.__closure__,strict=True))
    adapter=shared.INTERPOLATION_ADAPTER
    adapter_cells=dict(zip(adapter.__code__.co_freevars,adapter.__closure__,strict=True))
    if target=='class-method':
        monkeypatch.setattr(shared.Labels,'paint',shared.clone(shared.Labels.paint))
    elif target=='staticmethod-descriptor':
        monkeypatch.setattr(shared.Labels,'_create_brush_mask',staticmethod(shared.Labels._create_brush_mask))
    elif target=='property-descriptor':
        old=vars(shared.Labels)['brush_size']
        monkeypatch.setattr(shared.Labels,'brush_size',property(old.fget,old.fset,old.fdel,old.__doc__))
    elif target=='module-function':
        monkeypatch.setattr(shared.mouse,'pick',shared.clone(shared.mouse.pick))
    elif target=='registered-module':
        monkeypatch.setitem(sys.modules,'napari.layers.labels._labels_mouse_bindings',object())
    elif target=='defining-class':
        monkeypatch.setattr(labels_module,'Labels',type('Foreign',(),{}))
    elif target=='wrapper-binding':
        monkeypatch.setattr(shared.utils,'interpolate_coordinates',shared.clone(wrapper))
    elif target=='error-class-cell':
        monkeypatch.setattr(shared.WrongSelectedLabelError.__init__.__closure__[0],'cell_contents',ValueError)
    elif target=='wrapper-original-cell':
        monkeypatch.setattr(wrapper_cells['original'],'cell_contents',shared.clone(shared.INTERPOLATION))
    elif target=='adapter-original-cell':
        monkeypatch.setattr(adapter_cells['original'],'cell_contents',shared.clone(shared.INTERPOLATION))
    elif target=='wrapper-wrapped-alias':
        monkeypatch.setitem(wrapper.__dict__,'__wrapped__',shared.clone(shared.INTERPOLATION))
    elif target=='adapter-wrapped-alias':
        monkeypatch.setitem(adapter.__dict__,'__wrapped__',shared.clone(shared.INTERPOLATION))
    elif target=='wrapper-code':
        monkeypatch.setattr(wrapper,'__code__',wrapper.__code__.replace(co_firstlineno=wrapper.__code__.co_firstlineno+1))
    elif target=='function-defaults':
        monkeypatch.setattr(shared.ORIGINALS[5],'__defaults__',(object(),))
    elif target=='function-code':
        fn=shared.ORIGINALS[5]
        monkeypatch.setattr(fn,'__code__',fn.__code__.replace(co_firstlineno=fn.__code__.co_firstlineno+1))
    with pytest.raises(TypeError):
        snapshot.validate()
    with pytest.raises(TypeError):
        _=snapshot.prepared_codes
    # Only the explicit test-owned code mutations above may change a source.
    for entry,(function,code) in enumerate(zip(shared.ORIGINALS,before,strict=True)):
        if target=='function-code' and entry==5:
            continue
        assert function.__code__ is code


@pytest.mark.parametrize('target',['foreign-globals','wrong-code','property-subclass','missing-module','adapter-cell'])
def test_preflight_rejects_invalid_source_graph(target,monkeypatch):
    before=tuple(function.__code__ for function in shared.ORIGINALS)
    if target=='foreign-globals':
        original=shared.Labels.paint
        foreign=FunctionType(original.__code__,dict(original.__globals__),original.__name__,original.__defaults__,original.__closure__)
        monkeypatch.setattr(shared.Labels,'paint',foreign)
    elif target=='wrong-code':
        clone=shared.clone(shared.Labels.paint)
        clone.__code__=clone.__code__.replace(co_firstlineno=clone.__code__.co_firstlineno+1)
        monkeypatch.setattr(shared.Labels,'paint',clone)
    elif target=='property-subclass':
        class Foreign(property):
            @property
            def fget(self):
                raise AssertionError('foreign descriptor hook executed')
        monkeypatch.setattr(shared.Labels,'brush_size',Foreign(shared.Labels.brush_size.fget))
    elif target=='missing-module':
        monkeypatch.delitem(sys.modules,'napari.utils.interactions')
    elif target=='adapter-cell':
        adapter=shared.INTERPOLATION_ADAPTER
        captured=dict(zip(adapter.__code__.co_freevars,adapter.__closure__,strict=True))['original']
        monkeypatch.setattr(captured,'cell_contents',shared.clone(shared.INTERPOLATION))
    with pytest.raises(TypeError):
        prepare()
    assert all(function.__code__ is code for function,code in zip(shared.ORIGINALS,before,strict=True))


def test_snapshot_and_detached_function_cycle_are_visible_to_gc(monkeypatch):
    original=shared.Labels.paint
    clone=shared.clone(original)
    monkeypatch.setattr(shared.Labels,'paint',clone)
    snapshot=prepare()
    clone.__dict__['_snapshot_cycle']=snapshot
    references=weakref.ref(snapshot),weakref.ref(clone)
    assert gc.is_tracked(snapshot)
    monkeypatch.setattr(shared.Labels,'paint',original)
    # Monkeypatch retains the old value for teardown; release its undo log now.
    monkeypatch.undo()
    del snapshot,clone
    gc.collect()
    gc.collect()
    assert all(reference() is None for reference in references)
