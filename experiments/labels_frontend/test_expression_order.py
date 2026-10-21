"""Source-frame/cell and creation-order oracles for the native bridge."""
import builtins
import ctypes
import gc
import inspect
from pathlib import Path
import sys
from types import FunctionType, SimpleNamespace
import weakref

import numpy as np
import pytest

import _frontend_policy_probe as probe

BASE = Path(__file__).resolve().parent
sys.path.append(str(BASE))
import reference_bindings as shared
from inspect_inputs import walk

EXPRESSION_PROFILES = tuple(shared.tuples(row["shell_profile_18"]) for row in shared.FROZEN["expression_rows"])
ALL_CODES = list(walk(compile((BASE / "expression_shells.py").read_bytes(), str(BASE / "expression_shells.py"), "exec", dont_inherit=True)))
EXPRESSION_SHELLS = tuple(next(code for code in ALL_CODES if code.co_name == f"e{i}") for i in range(7))
PARENTS = tuple(shared.ORIGINALS[i] for i in (10,11,29,3))
CODES = probe.prepare_expression_codes_probe(PARENTS,shared.SOURCE_PROFILES,EXPRESSION_SHELLS,EXPRESSION_PROFILES)
MAPPING = ((10,0),(10,1),(11,0),(29,0),(3,0),(3,1),(3,2))
ORIGINAL_CODES = tuple([value for value in shared.ORIGINALS[parent].__code__.co_consts
                       if hasattr(value,"co_code") and value.co_name == "<genexpr>"][ordinal]
                      for parent,ordinal in MAPPING)


def cell(value):
    return (lambda: value).__closure__[0]


def prepare(entry,function=None):
    function = shared.clone(shared.ORIGINALS[entry]) if function is None else function
    function.__code__ = probe.prepare_unified_expression_probe(function,entry,shared.SHELLS,
        shared.SOURCE_PROFILES,shared.SHELL_PROFILES,CODES)
    return function


def generator(kind,native,items,captures=None,namespace=None):
    captures = captures or {}
    source = ORIGINAL_CODES[kind]
    closure = tuple(cell(captures[name]) for name in source.co_freevars)
    namespace = dict(shared.ORIGINALS[MAPPING[kind][0]].__globals__) if namespace is None else namespace
    function = FunctionType(CODES[kind] if native else source,namespace,closure=closure or None)
    iterator = iter(items)
    result = function(iterator)
    assert inspect.isgenerator(result) and inspect.isgeneratorfunction(function)
    assert result.gi_frame.f_globals is namespace
    assert result.gi_frame.f_locals[".0"] is iterator
    assert result.gi_code.co_freevars == source.co_freevars
    assert result.gi_code.co_filename == source.co_filename
    assert result.gi_code.co_qualname == source.co_qualname
    return result


def outcome(call):
    try:
        return ("return",shared.freeze(call()))
    except BaseException as error:
        return ("raise",type(error).__name__,str(error),getattr(error,"name",None),
                None if error.__cause__ is None else type(error.__cause__).__name__)


@pytest.mark.parametrize("kind,items,captures", [
    (0,[3,4],{}), (1,[0,1],{"grids":np.array([[0,1],[2,3]]),"center":np.array([1,2]),"normalized_scale":np.array([2,3])}),
    (2,[0,1,2],{"i":1}), (3,["Alt","Control"],{"event":SimpleNamespace(modifiers=("Alt",))}),
    (4,[2,0],{"int_coord":(3,4,5)}), (5,[0,1],{}), (6,[(0,2),(3,4)],{}),
])
def test_all_seven_source_named_generator_frames(kind,items,captures):
    actual = generator(kind,True,items,captures)
    expected = generator(kind,False,items,captures)
    assert [shared.freeze(value) for value in actual] == [shared.freeze(value) for value in expected]


@pytest.mark.parametrize("started,action", [(False,"send"),(True,"send"),(False,"throw"),
    (True,"throw"),(False,"close"),(True,"close")])
def test_protocol_with_real_expression_frames(started,action):
    def run(native):
        gen=generator(0,native,[2,3])
        values=[]
        if started:
            values.append(outcome(lambda: next(gen)))
        operation={"send":lambda:gen.send(7),"throw":lambda:gen.throw(StopIteration("injected")),"close":gen.close}[action]
        values.extend([outcome(operation),outcome(lambda:next(gen)),outcome(gen.close)])
        return values
    assert run(True)==run(False)


@pytest.mark.parametrize("phase", ["rebind-before-body","rebind-during-consumer-lookup"])
def test_make_function_snapshots_builtins_before_get_iter(phase):
    def run(native):
        log=[]
        source=shared.ORIGINALS[10]
        namespace=dict(source.__globals__)
        for name in ("slice","tuple","range","len","sum"):
            namespace.pop(name,None)

        def special_slice(*args):
            caller=inspect.currentframe().f_back
            log.append(("slice-B1",args,caller.f_builtins is snapshot,
                        caller.f_globals is namespace,caller.f_code.co_qualname))
            return builtins.slice(*args)

        snapshot=dict(vars(builtins),slice=special_slice,marker="B1")
        later=dict(vars(builtins),marker="B2")

        class Before(dict):
            fired=False
            def __getitem__(self,name):
                if phase=="rebind-during-consumer-lookup" and name=="tuple" and not self.fired:
                    self.fired=True
                    log.append("consumer-lookup")
                    namespace["__builtins__"]=snapshot
                return super().__getitem__(name)

        before=Before(vars(builtins))
        namespace["__builtins__"]=before
        function=FunctionType(source.__code__,namespace)
        if native:
            prepare(10,function)
        if phase=="rebind-before-body":
            namespace["__builtins__"]=snapshot

        class Shape:
            def __len__(self):
                return 2
            def __iter__(self):
                log.append("get-iter")
                namespace["__builtins__"]=later
                return iter((3,4))

        result=outcome(lambda:function(Shape(),1.5,np.array([1,2]),np.array([1,1])))
        slices=[row for row in log if isinstance(row,tuple) and row[0]=="slice-B1"]
        assert len(slices)==2 and all(row[2] and row[3] for row in slices)
        assert namespace["__builtins__"] is later and function.__builtins__ is before
        return result,log
    assert run(True)==run(False)


def test_escaped_bbox_generators_share_source_named_cell_and_frame_mutation():
    def run(native):
        escaped=[]
        def retaining_tuple(value):
            assert inspect.isgenerator(value)
            escaped.append(value)
            return ()
        source=shared.ORIGINALS[11]
        function=FunctionType(source.__code__,dict(source.__globals__,tuple=retaining_tuple))
        if native:
            prepare(11,function)
        result=outcome(lambda:function(np.eye(2,dtype=bool)))
        assert len(escaped)==2
        assert all(gen.gi_frame.f_locals["i"]==1 for gen in escaped)
        frame=escaped[0].gi_frame
        locals_=frame.f_locals
        locals_["i"]=0
        sync=ctypes.pythonapi.PyFrame_LocalsToFast
        sync.argtypes=(ctypes.py_object,ctypes.c_int)
        sync.restype=None
        sync(frame,0)
        assert escaped[1].gi_frame.f_locals["i"]==0
        return result,[list(gen) for gen in escaped]
    assert run(True)==run(False)


@pytest.mark.parametrize("entry,args", [(10,((3,4),1.5,np.array([1,2]),np.array([1,2]))),
    (11,(np.eye(4,dtype=bool),)),
    (3,(SimpleNamespace(data=np.ones((2,3)),preserve_labels=False,contiguous=False),(0,0),5,[0,1]))])
def test_parent_numeric_policy_through_new_expression_frames(entry,args):
    assert outcome(lambda:prepare(entry)(*args)) == outcome(lambda:shared.ORIGINALS[entry](*args))


def test_captured_event_cycle_is_visible_and_released():
    class Event:
        modifiers=("Alt",)
    event=Event()
    gen=generator(3,True,["Alt","Control"],{"event":event})
    event.generator=gen
    assert next(gen) is True
    state=gen.gi_frame.f_locals["_state"]
    assert gc.is_tracked(state)
    refs=weakref.ref(event),weakref.ref(gen),weakref.ref(state)
    del event,gen,state
    gc.collect()
    gc.collect()
    assert all(reference() is None for reference in refs)


def test_modifier_parent_captures_own_builtins_and_named_event_cell():
    def run(native):
        log=[]
        source=shared.ORIGINALS[29]
        namespace=dict(source.__globals__)
        before=dict(vars(builtins),marker="parent")
        snapshot=dict(vars(builtins),marker="expression")
        later=dict(vars(builtins),marker="after-iter")
        namespace["__builtins__"]=before

        class Parts:
            def __iter__(self):
                namespace["__builtins__"]=later
                return iter(("Alt",))

        class Event:
            pos=(3,4)
            @property
            def modifiers(self):
                caller=inspect.currentframe().f_back
                log.append((caller.f_builtins is snapshot,caller.f_globals is namespace,
                            caller.f_locals["event"] is self,caller.f_code.co_freevars))
                return ("Alt",)

        namespace["BRUSH_SIZE_ON_MOUSE_MOVE_MODIFIERS_PARTS"]=Parts()
        function=FunctionType(source.__code__,namespace)
        if native:
            prepare(29,function)
        namespace["__builtins__"]=snapshot
        obj=SimpleNamespace(min_brush_size=1,init_pos=None,init_brush_size=None)
        layer=SimpleNamespace(brush_size=10,cursor="circle")
        result=outcome(lambda:function(obj,layer,Event()))
        assert log==[(True,True,True,("event",))]
        return result,vars(obj),vars(layer),log
    assert run(True)==run(False)


def test_flood_bbox_function_precedes_foreign_zip_call():
    def run(native):
        log=[]
        source=shared.ORIGINALS[3]
        namespace=dict(source.__globals__)
        namespace.pop("slice",None)
        before=dict(vars(builtins),marker="parent")
        later=dict(vars(builtins),marker="after-zip")

        def marked_slice(*args):
            caller=inspect.currentframe().f_back
            log.append(("slice",tuple(int(v) for v in args),caller.f_builtins is snapshot,
                        caller.f_code.co_firstlineno))
            return builtins.slice(*args)

        snapshot=dict(vars(builtins),slice=marked_slice,marker="expression")
        namespace["__builtins__"]=before
        def bounds(mask):
            namespace["__builtins__"]=snapshot
            return np.array([0,0]),np.array([2,2])
        def foreign_zip(*args,**kwargs):
            log.append("zip-call")
            namespace["__builtins__"]=later
            return builtins.zip(*args,**kwargs)
        namespace["zip"]=foreign_zip
        function=FunctionType(source.__code__,namespace)
        if native:
            prepare(3,function)
        layer=SimpleNamespace(data=np.eye(2),preserve_labels=False,contiguous=False,_compute_mask_bbox=bounds)
        result=outcome(lambda:function(layer,(0,0),5,[0,1]))
        slices=[row for row in log if isinstance(row,tuple)]
        assert len(slices)==2 and all(row[2] for row in slices)
        assert all(row[3]==ORIGINAL_CODES[6].co_firstlineno for row in slices)
        return result,log
    assert run(True)==run(False)


@pytest.mark.parametrize("tamper", ["parent","shell"])
def test_full_expression_profiles_reject_mismatch(tamper):
    parents=list(PARENTS)
    shells=list(EXPRESSION_SHELLS)
    if tamper=="parent":
        parents[0]=shared.clone(parents[0])
        parents[0].__code__=parents[0].__code__.replace(co_firstlineno=parents[0].__code__.co_firstlineno+1)
    else:
        shells[0]=shells[0].replace(co_firstlineno=shells[0].co_firstlineno+1)
    with pytest.raises(TypeError):
        probe.prepare_expression_codes_probe(tuple(parents),shared.SOURCE_PROFILES,tuple(shells),EXPRESSION_PROFILES)
