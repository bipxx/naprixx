"""Stdlib-only grid frame controls, also runnable with the 3.11/3.13 SDKs."""
import builtins
import ctypes
import hashlib
import json
import math
from pathlib import Path
import sys
import types

from freeze_transport import profile
from inspect_inputs import walk

DIRECTORY = Path(__file__).resolve().parent
ROOT = DIRECTORY.parents[1]


def tuples(value):
    return tuple(map(tuples, value)) if isinstance(value, list) else value


def deny_archive(event, args):
    if event in ('open', 'os.listdir', 'os.scandir') and args and isinstance(args[0], (str, bytes)):
        name = args[0].decode() if isinstance(args[0], bytes) else args[0]
        if '.artifacts' in Path(name).parts:
            raise AssertionError('archive access denied')


class Array:
    def __init__(self, values):
        self.values = list(values)

    def __mul__(self, value):
        return Array([item * value for item in self.values])

    def __add__(self, value):
        return Array([item + value for item in self.values])

    def __getitem__(self, key):
        return self


def run(code, native, shells, probe, mode):
    events = []
    local_events = []
    retained_frames = []
    namespace = {'math': math, '__name__': 'numpy.lib._index_tricks_impl'}
    snapshots = [dict(vars(builtins), grid_marker=f'B{i}') for i in range(4)]
    iterator_anchor = []
    length_calls = 0

    def length_callback(value):
        nonlocal length_calls
        length_calls += 1
        if length_calls == 2:
            # zip must already have been loaded before len(size) is called.
            namespace['zip'] = lambda *args: (_ for _ in ()).throw(AssertionError('late zip lookup'))
        return len(value)

    class Iterator:
        def __init__(self, items):
            self.items = iter(items)

        def __iter__(self):
            namespace['__builtins__'] = snapshots[3]
            return self

        def __next__(self):
            return next(self.items)

    def zip_callback(*args):
        namespace['__builtins__'] = snapshots[2]
        result = Iterator(zip(*args))
        iterator_anchor.append(result)
        return result

    def arange(value, *, dtype):
        frame = sys._getframe(1)
        events.append((frame.f_code.co_name, frame.f_code.co_qualname,
                       frame.f_builtins['grid_marker'], frame.f_globals is namespace,
                       frame.f_locals.get('.0') is iterator_anchor[0]))
        if mode == 'raise':
            raise RuntimeError('arange callback failure')
        return Array(range(value))

    def local_snapshot(frame):
        values = frame.f_locals
        return (tuple(sorted(values)), values.get('_x'),
                getattr(values.get('_t'), '__name__', None))

    class Numeric:
        def __getattribute__(self, name):
            if name == 'arange' and mode in ('inspect-locals', 'mutate-locals', 'delete-local'):
                frame = sys._getframe(1)
                retained_frames.append(frame)
                local_events.append(('lookup', local_snapshot(frame)))
                values = frame.f_locals
                if mode == 'mutate-locals':
                    values['_x'], values['_t'] = 3, float
                elif mode == 'delete-local':
                    del values['_x']
                if mode != 'inspect-locals':
                    sync = ctypes.pythonapi.PyFrame_LocalsToFast
                    sync.argtypes = [ctypes.py_object, ctypes.c_int]
                    sync.restype = None
                    sync(frame, 1)
            return object.__getattribute__(self, name)

    numeric = Numeric()
    numeric.complexfloating = type('Complex', (), {})
    numeric.result_type = lambda *values: int
    numeric.arange = arange
    numeric.newaxis = None
    namespace['_nx'] = numeric
    snapshots[0]['zip'] = zip_callback
    snapshots[0]['len'] = length_callback
    namespace['__builtins__'] = snapshots[0]
    function = types.FunctionType(code, namespace)
    original_codes = {item for item in walk(code)}
    if native:
        replacement = probe.prepare_grid_function_probe(function, namespace, shells['_grid_getitem'],
            tuples(profile(code)), tuples(profile(shells['_grid_getitem'].__code__)), 0,
            shells['_grid_sparse_comp'], tuples(profile(shells['_grid_sparse_comp'].__code__)))
        assert isinstance(replacement, types.CodeType)
        function.__code__ = replacement
        def guard(frame, event, arg):
            if event == 'call' and frame.f_code in original_codes:
                raise AssertionError('original grid body executed')
        sys.setprofile(guard)
    namespace['__builtins__'] = snapshots[1]
    try:
        try:
            result = function(types.SimpleNamespace(sparse=True), (slice(1, 5, 2), slice(0, 3)))
            outcome = ['return', [item.values for item in result]]
        except Exception as error:
            outcome = ['raise', type(error).__name__, str(error)]
    finally:
        sys.setprofile(None)
    expected_name = '<listcomp>' if sys.version_info < (3, 12) else '__getitem__'
    expected_marker = 'B1' if sys.version_info < (3, 12) else 'B0'
    assert (events or mode == 'delete-local') and all(
        row[0] == expected_name and row[2] == expected_marker and row[3] for row in events), events
    assert all(row[4] == (sys.version_info < (3, 12)) for row in events), events
    if retained_frames:
        local_events.append(('retained', local_snapshot(retained_frames[-1])))
    if mode == 'mutate-locals':
        assert outcome == ['return', [[1, 3, 5], [0, 1, 2]]], outcome
    if mode == 'delete-local':
        assert outcome == ['raise', 'UnboundLocalError',
            "cannot access local variable '_x' where it is not associated with a value"], outcome
    return outcome, events, local_events


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    pins = json.loads((DIRECTORY / 'grid_provider_controls.json').read_text())
    row = pins['sources'][0]
    path = ROOT / '.venv/Lib/site-packages' / row['package_path']
    data = path.read_bytes()
    assert hashlib.sha256(data).hexdigest() == row['sha256']
    code = next(item for item in walk(compile(data, str(path), 'exec', dont_inherit=True))
                if item.co_qualname == row['qualname'])
    shells = {}
    path = DIRECTORY / 'grid_shells.py'
    exec(compile(path.read_bytes(), str(path), 'exec', dont_inherit=True), shells)
    modes = ['return', 'raise']
    if sys.version_info < (3, 12):
        modes += ['inspect-locals', 'mutate-locals', 'delete-local']
    for mode in modes:
        original = run(code, False, shells, probe, mode)
        native = run(code, True, shells, probe, mode)
        assert original == native, (mode, original, native)
    print(json.dumps({'status': 'PASS', 'python': sys.version, 'controls': len(modes),
                      'original_body_execution_in_native_run': False}))


if __name__ == '__main__':
    main()
