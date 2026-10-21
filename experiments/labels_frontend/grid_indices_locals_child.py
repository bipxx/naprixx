"""Compare real indices source locals with the native frame on CPython 3.11–3.13."""
import builtins
import ctypes
import hashlib
import json
from pathlib import Path
import sys
import types

from freeze_transport import profile
from grid_comprehension_child import deny_archive, tuples
from inspect_inputs import walk

DIRECTORY = Path(__file__).resolve().parent
ROOT = DIRECTORY.parents[1]
MODES = ('dense', 'sparse', 'empty-dense', 'empty-sparse', 'mutate-len',
         'mutate-arange', 'reshape-lookup', 'mutate-reshape', 'delete-dtype',
         'delete-res', 'callback-error')
if sys.version_info[:2] == (3, 12):
    # Clearing a definitely-initialized argument with the C API makes the
    # source's unchecked LOAD_FAST consume NULL on this release interpreter.
    # That low-level behavior needs its own ABI decision; it is not qualified
    # by a fabricated UnboundLocalError oracle. Keep the failing observation.
    MODES = tuple(mode for mode in MODES if not mode.startswith('delete-'))


def run(probe, code, shell, mode, native, invoke=None, observe=None):
    events = []
    retained = []
    called = set()

    class Array:
        def __init__(self, values, shape=None):
            self.values, self.shape = list(values), shape

        def __getattribute__(self, name):
            if name == 'reshape':
                callback('reshape-lookup', sys._getframe(1))
            return object.__getattribute__(self, name)

        def reshape(self, shape):
            callback('reshape-call', sys._getframe(1), tuple(shape))
            return Array(self.values, tuple(shape))

    class Dense:
        def __init__(self, shape, dtype):
            self.shape, self.dtype, self.items = shape, dtype, {}

        def __setitem__(self, key, value):
            callback('store', sys._getframe(1), key)
            self.items[key] = value

    def normalize(value):
        if isinstance(value, Array):
            return ('array', value.values, value.shape)
        if isinstance(value, Dense):
            return ('dense', value.shape, value.dtype.__name__,
                    sorted((key, normalize(item)) for key, item in value.items.items()))
        if isinstance(value, (list, tuple)):
            return tuple(normalize(item) for item in value)
        if isinstance(value, type):
            return ('type', value.__name__)
        return value

    def snapshot(frame):
        return tuple((name, normalize(value)) for name, value in sorted(frame.f_locals.items()))

    def write(frame, updates=(), delete=()):
        values = frame.f_locals
        for key, value in updates:
            values[key] = value
        for key in delete:
            del values[key]
        if sys.version_info < (3, 13):
            sync = ctypes.pythonapi.PyFrame_LocalsToFast
            sync.argtypes = [ctypes.py_object, ctypes.c_int]
            sync.restype = None
            sync(frame, 1)

    def callback(event, frame, *args):
        assert frame.f_code.co_name == 'indices' and frame.f_globals is namespace
        if observe is not None:observe(event,frame)
        retained.append(frame)
        events.append((event, args, snapshot(frame)))
        if event in called:
            return
        called.add(event)
        if event == 'len' and mode == 'mutate-len':
            write(frame, (('dimensions', (4,)), ('dtype', float)))
        elif event == 'arange' and mode == 'mutate-arange':
            write(frame, (('dim', 5), ('i', 1), ('shape', (9, 1)), ('sparse', True), ('res', ())))
        elif event == 'reshape-lookup' and mode == 'reshape-lookup':
            write(frame, (('shape', (7, 1)), ('dim', 4)))
        elif event == 'reshape-call' and mode == 'mutate-reshape':
            write(frame, (('res', ()), ('sparse', True), ('idx', 'overwritten-by-return')))
        elif event == 'len' and mode == 'delete-dtype':
            write(frame, delete=('dtype',))
        elif event == 'arange' and mode == 'delete-res':
            write(frame, delete=('res',))
        elif event == 'arange' and mode == 'callback-error':
            raise RuntimeError('indices callback failure')

    def tuple_callback(value):
        callback('tuple', sys._getframe(1))
        return tuple(value)

    def len_callback(value):
        callback('len', sys._getframe(1))
        return len(value)

    def enumerate_callback(value):
        callback('enumerate', sys._getframe(1))
        return enumerate(value)

    def empty(shape, *, dtype):
        callback('empty', sys._getframe(1), shape, dtype.__name__)
        return Dense(shape, dtype)

    def arange(dim, *, dtype):
        callback('arange', sys._getframe(1), dim, dtype.__name__)
        return Array(range(dim))

    namespace = {'__name__': 'numpy._core.numeric', '__builtins__': vars(builtins),
                 'tuple': tuple_callback, 'len': len_callback, 'enumerate': enumerate_callback,
                 'empty': empty, 'arange': arange}
    function = types.FunctionType(code, namespace, argdefs=(int, False))
    if native:
        replacement = probe.prepare_grid_function_probe(function, namespace, shell,
            tuples(profile(code)), tuples(profile(shell.__code__)), 1)
        assert isinstance(replacement, types.CodeType)
        function.__code__ = replacement
        def guard(frame, event, arg):
            if event == 'call' and frame.f_code is code:
                raise AssertionError('original indices body executed')
        sys.setprofile(guard)
    try:
        try:
            arguments = (() if mode.startswith('empty-') else (2, 3), int,
                         mode in ('sparse', 'empty-sparse'))
            result = function(*arguments) if invoke is None else invoke(function,arguments)
            outcome = ('return', normalize(result))
        except Exception as error:
            outcome = ('raise', type(error).__name__, str(error))
            if mode not in ('delete-dtype', 'delete-res', 'callback-error'):
                raise
    finally:
        sys.setprofile(None)
    assert events
    if mode == 'callback-error':
        assert outcome == ('raise', 'RuntimeError', 'indices callback failure'), outcome
    if mode.startswith('delete-') and sys.version_info < (3, 12):
        name = 'dtype' if mode == 'delete-dtype' else 'res'
        assert outcome == ('raise', 'UnboundLocalError',
            f"cannot access local variable '{name}' where it is not associated with a value"), outcome
    if mode.startswith('delete-') and sys.version_info >= (3, 13):
        # The interpreter's write-through proxy rejects deletion itself.
        assert outcome[0] == 'raise', outcome
    return outcome, events, snapshot(retained[-1])


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row = json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][1]
    path = ROOT/'.venv/Lib/site-packages'/row['package_path']
    source = path.read_bytes()
    assert hashlib.sha256(source).hexdigest() == row['sha256']
    code = next(item for item in walk(compile(source, str(path), 'exec', dont_inherit=True))
                if item.co_qualname == row['qualname'])
    path = DIRECTORY/'grid_shells.py'
    shells = {}
    exec(compile(path.read_bytes(), str(path), 'exec', dont_inherit=True), shells)
    for mode in MODES:
        before = run(probe, code, shells['_grid_indices'], mode, False)
        after = run(probe, code, shells['_grid_indices'], mode, True)
        assert before == after, (mode, before, after)
    print(json.dumps({'status': 'PASS', 'python': sys.version, 'controls': len(MODES),
                      'original_indices_execution_in_native_run': False}))


if __name__ == '__main__':
    main()
