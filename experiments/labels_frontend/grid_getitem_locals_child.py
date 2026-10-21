"""Source comparisons for grid parent locals and PEP 709 slot restoration."""
import builtins
import ctypes
import hashlib
import json
import math
from pathlib import Path
import sys
import types

from freeze_transport import profile
from grid_comprehension_child import deny_archive, tuples
from inspect_inputs import walk

DIRECTORY = Path(__file__).resolve().parent
ROOT = DIRECTORY.parents[1]
MODES = ['sparse', 'dense', 'single', 'complex-single', 'complex-grid',
         'mutate-result', 'mutate-slice', 'mutate-key', 'mutate-inline', 'callback-error']
if sys.version_info >= (3, 12):
    MODES += ['restore', 'restore-error']


def run(probe, code, shells, mode, native):
    events, retained, parents, called = [], [], [], set()

    def normalize(value):
        if isinstance(value, Array): return ('array', tuple(value.values))
        if isinstance(value, Dense): return ('dense', tuple(normalize(item) for item in value.items))
        if isinstance(value, Slice): return ('slice', value.__dict__['start'], value.__dict__['stop'], value.__dict__['step'])
        if isinstance(value, slice): return ('slice', value.start, value.stop, value.step)
        if isinstance(value, types.SimpleNamespace): return ('grid', value.sparse)
        if isinstance(value, (tuple, list)): return tuple(normalize(item) for item in value)
        if isinstance(value, type): return ('type', value.__name__)
        if value is None or isinstance(value, (int, float, str, complex)): return value
        return ('object', type(value).__name__)

    def snapshot(frame):
        return tuple((name, normalize(value)) for name, value in sorted(frame.f_locals.items()))

    def write(frame, **updates):
        values = frame.f_locals
        values.update(updates)
        if sys.version_info < (3, 13):
            sync = ctypes.pythonapi.PyFrame_LocalsToFast
            sync.argtypes, sync.restype = [ctypes.py_object, ctypes.c_int], None
            sync(frame, 1)

    def callback(event, frame, *args):
        assert frame.f_code.co_name in ('__getitem__', '<listcomp>') and frame.f_globals is namespace
        retained.append(frame)
        if frame.f_code.co_name == '__getitem__': parents.append(frame)
        error = sys.exception()
        events.append((event, tuple(normalize(item) for item in args), snapshot(frame),
                       None if error is None else (type(error).__name__, str(error))))
        if event in called: return
        called.add(event)
        if event == 'result_type' and mode == 'mutate-result':
            write(frame, size=[3, 4], num_list=[9])
        elif event == 'slice-step' and mode == 'mutate-slice':
            write(frame, k=1)
        elif event == 'len' and mode == 'mutate-key':
            write(frame, key=(Slice(2, 6, 2), Slice(1, 4, 1)))
        elif event == 'lookup-arange' and mode == 'mutate-inline':
            write(frame, _x=4, _t=float)
        elif event == 'zip' and mode in ('restore', 'restore-error'):
            write(frame, _x=73, _t=str)
        elif event == 'arange' and mode == 'restore-error':
            raise RuntimeError('inline body failure')
        elif event == 'result_type' and mode == 'callback-error':
            raise RuntimeError('result type failure')

    class Slice:
        def __init__(self, start, stop, step):
            self.start, self.stop, self.step = start, stop, step

        def __getattribute__(self, name):
            if name in ('step', 'start', 'stop'):
                callback('slice-'+name, sys._getframe(1))
            return object.__getattribute__(self, name)

    class Array:
        def __init__(self, values): self.values = list(values)
        def __mul__(self, other):
            callback('multiply', sys._getframe(1), other)
            return Array([item*other for item in self.values])
        def __add__(self, other):
            callback('add', sys._getframe(1), other)
            return Array([item+other for item in self.values])
        def __getitem__(self, key):
            callback('array-slice', sys._getframe(1), key)
            return Array(self.values)

    class Dense:
        def __init__(self, items): self.items = list(items)
        def __getitem__(self, key):
            callback('dense-get', sys._getframe(1), key)
            return self.items[key]
        def __setitem__(self, key, value):
            callback('dense-set', sys._getframe(1), key)
            self.items[key] = value

    class Numeric:
        def __getattribute__(self, name):
            if name in ('complexfloating', 'result_type', 'arange', 'indices', 'newaxis'):
                callback('lookup-'+name, sys._getframe(1))
            return object.__getattribute__(self, name)

    def result_type(*args):
        callback('result_type', sys._getframe(1), *args)
        return float if mode.startswith('complex-') else int

    def arange(*args, dtype=int):
        callback('arange', sys._getframe(1), *args, dtype)
        start, stop, step = (0, args[0], 1) if len(args)==1 else args
        values = []
        while start < stop if step > 0 else start > stop:
            values.append(start); start += step
        return Array(values)

    def indices(size, typ):
        callback('indices', sys._getframe(1), size, typ)
        return Dense(Array(range(item)) for item in size)

    def wrapped(name):
        function = getattr(builtins, name)
        def call(*args):
            callback(name, sys._getframe(1), *args)
            return function(*args)
        return call

    numeric = Numeric()
    numeric.complexfloating = type('ComplexFloating', (), {})
    numeric.result_type, numeric.arange, numeric.indices, numeric.newaxis = result_type, arange, indices, None
    namespace = {'__name__':'numpy.lib._index_tricks_impl', '__builtins__':vars(builtins),
                 'math':math, '_nx':numeric}
    namespace.update({name:wrapped(name) for name in ('len','range','zip','enumerate','slice','tuple')})
    function = types.FunctionType(code, namespace)
    originals = set(walk(code))
    if native:
        replacement = probe.prepare_grid_function_probe(function, namespace, shells['_grid_getitem'],
            tuples(profile(code)), tuples(profile(shells['_grid_getitem'].__code__)), 0,
            shells['_grid_sparse_comp'], tuples(profile(shells['_grid_sparse_comp'].__code__)))
        assert isinstance(replacement, types.CodeType)
        function.__code__ = replacement
        def guard(frame,event,arg):
            if event=='call' and frame.f_code in originals: raise AssertionError('original grid body executed')
        sys.setprofile(guard)
    key = Slice(1,5,2) if mode=='single' else Slice(1,5,3j) if mode=='complex-single' else (
        Slice(1,5,3j if mode=='complex-grid' else 2), Slice(0,3,1))
    try:
        try:
            outcome = ('return',normalize(function(types.SimpleNamespace(sparse=mode!='dense'), key)))
        except RuntimeError as error:
            outcome = ('raise',type(error).__name__,str(error))
            assert mode in ('restore-error','callback-error')
    finally:
        sys.setprofile(None)
    assert events and parents
    if mode=='restore-error': assert outcome==('raise','RuntimeError','inline body failure')
    if mode=='callback-error': assert outcome==('raise','RuntimeError','result type failure')
    if mode in ('restore','restore-error'):
        values = parents[-1].f_locals
        assert values['_x']==73 and values['_t'] is str, snapshot(parents[-1])
    return outcome, events, snapshot(parents[-1]), snapshot(retained[-1])


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row = json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path = ROOT/'.venv/Lib/site-packages'/row['package_path']
    source = path.read_bytes()
    assert hashlib.sha256(source).hexdigest()==row['sha256']
    code = next(item for item in walk(compile(source,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells = {}; path = DIRECTORY/'grid_shells.py'
    exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    for mode in MODES:
        before = run(probe,code,shells,mode,False)
        after = run(probe,code,shells,mode,True)
        assert before==after,(mode,before,after)
    print(json.dumps({'status':'PASS','python':sys.version,'controls':len(MODES),
                      'original_grid_execution_in_native_run':False}))


if __name__=='__main__': main()
