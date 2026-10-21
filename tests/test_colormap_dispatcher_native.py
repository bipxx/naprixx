"""Eight finite optional dispatcher/reload/reference comparisons.

The oracle imports an exact temporary copy of the pinned accelerated module,
using real Numba and original njit initialization. NumPy, Numba typed containers,
and the unchanged enum's attributes are dependency/metadata boundaries. PartSeg
availability is controlled absent here. These cases do not establish PartSeg,
the full Numba dtype/signature matrix, or arbitrary CPUDispatcher introspection.

Three cases reload the actual installed fork module and the complete temporary
original module. Metadata/GC cases isolate the two exact inner-function globals;
cache is disabled only for those unexecuted scratch dispatchers. No native
production call delegates to an original function in these comparisons.
"""

from __future__ import annotations

import gc
import hashlib
import importlib
import inspect
import sys
import warnings
import weakref
from pathlib import Path
from types import FunctionType

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / ".upstream/napari/src/napari/utils/colormaps/_accelerated_cmap.py"
PINNED_COMMIT = "595639295a153c84bcb127a9e07d801355440a35"
PINNED_SHA256 = "ac4b1f4cb87980857811112d48676987e4fa722078f783681186b101a559d6b1"
INNER_NAMES = (
    "_zero_preserving_modulo_inner_loop",
    "_labels_raw_to_texture_direct_inner_loop",
)


@pytest.fixture(scope="module")
def implementations(tmp_path_factory):
    numba = pytest.importorskip("numba", reason="real original JIT oracle required")
    raw = SOURCE.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED_SHA256
    directory = tmp_path_factory.mktemp("original_colormap_dispatcher")
    module_name = "_naparixx_pinned_colormap_dispatcher_contract"
    assert module_name not in sys.modules
    path = directory / f"{module_name}.py"
    with path.open("xb") as stream:
        stream.write(raw)
    assert hashlib.sha256(path.read_bytes()).hexdigest() == PINNED_SHA256
    native = importlib.import_module("naparixx._native")
    assert callable(native.colormap_compile_inner_loops)
    # The temporary module is discoverable through the ordinary import system,
    # so importlib.reload executes all original initialization, not a simulator.
    path_entry = str(directory)
    sys.path.insert(0, path_entry)
    try:
        with pytest.MonkeyPatch.context() as patch:
            patch.setitem(sys.modules, "PartSegCore_compiled_backend", None)
            original = importlib.import_module(module_name)
            current = importlib.import_module(
                "napari.utils.colormaps._accelerated_cmap")
            # An earlier importer may have observed a different dependency set.
            current = importlib.reload(current)
            assert Path(original.__file__).resolve() == path.resolve()
            assert Path(current.__file__).read_bytes() == (
                ROOT / "src/napari/utils/colormaps/_accelerated_cmap.py"
            ).read_bytes()
            for name, class_name in zip(INNER_NAMES, (
                "ColormapModuloDispatcher", "ColormapDirectDispatcher",
            ), strict=True):
                assert callable(getattr(original, name).py_func)
                assert callable(getattr(current, name).py_func)
                assert isinstance(getattr(current, name), getattr(native, class_name))
            yield original, current, numba, native
            # Leave the actual application initialized for its real dependencies.
        importlib.reload(current)
    finally:
        sys.modules.pop(module_name, None)
        sys.path.remove(path_entry)


def _plain(function):
    return getattr(function, "py_func", function)


def _scratch(module, numba, native, *, current, enabled=True):
    namespace = dict(vars(module))
    namespace.update(numba=numba if enabled else None, prange=range, _native=native)
    for name in INNER_NAMES:
        source = _plain(getattr(module, name))
        assert isinstance(source, FunctionType)
        assert source.__closure__ is None
        function = FunctionType(source.__code__, namespace, source.__name__,
                                source.__defaults__)
        function.__qualname__ = source.__qualname__
        function.__module__ = source.__module__
        function.__annotations__ = source.__annotations__
        function.__kwdefaults__ = source.__kwdefaults__
        function.__dict__.update(source.__dict__)
        namespace[name] = function
    originals = tuple(namespace[name] for name in INNER_NAMES)
    if current:
        native.colormap_compile_inner_loops(namespace)
    elif enabled:
        # Real njit creates the original dispatcher. Caching is irrelevant to
        # these unexecuted scratch instances and would retain test artifacts.
        for name in INNER_NAMES:
            namespace[name] = numba.njit(parallel=True, cache=False)(namespace[name])
    return namespace, originals


def _modulo(function):
    values = np.array([-128, -1, 0, 1, 127], dtype=np.int8)
    out = np.full(values.shape, -91, dtype=np.int64)
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always", RuntimeWarning)
        result = function(values, 3, 1, out)
    return (out.tolist(), result is out,
            tuple((type(item.message).__name__, str(item.message)) for item in caught))


def _outer(function):
    values = np.array([-128, -1, 0, 1, 127], dtype=np.int8)
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always", RuntimeWarning)
        result = function(values, 3, np.int64, 1)
    return (result.tolist(), result.dtype.str,
            tuple((type(item.message).__name__, str(item.message)) for item in caught))


def _reload(module, numba, enabled):
    with pytest.MonkeyPatch.context() as patch:
        patch.setitem(sys.modules, "numba", numba if enabled else None)
        patch.setitem(sys.modules, "PartSegCore_compiled_backend", None)
        return importlib.reload(module)


@pytest.mark.parametrize("name", INNER_NAMES)
def test_dispatcher_preserves_exact_py_func_and_standard_metadata(
    implementations, name,
):
    original, current, numba, native = implementations
    outcomes = []
    for module, is_current in ((original, False), (current, True)):
        namespace, functions = _scratch(module, numba, native, current=is_current)
        function = functions[INNER_NAMES.index(name)]
        dispatcher = namespace[name]
        outcomes.append((
            dispatcher.py_func is function,
            dispatcher.__wrapped__ is function,
            inspect.unwrap(dispatcher) is function,
            inspect.signature(dispatcher) == inspect.signature(function),
            tuple(getattr(dispatcher, attribute) == getattr(function, attribute)
                  for attribute in ("__name__", "__qualname__", "__module__",
                                    "__doc__", "__annotations__")),
        ))
    assert outcomes[0] == (True, True, True, True, (True,) * 5)
    assert outcomes[1] == outcomes[0]


def test_actual_reload_retains_compiled_alias_and_plain_py_func_mode(implementations):
    original, current, numba, _ = implementations
    outcomes = []
    for module in (original, current):
        module = _reload(module, numba, True)
        compiled = module._zero_preserving_modulo_inner_loop
        plain = compiled.py_func
        before = _modulo(compiled), _modulo(plain)
        module = _reload(module, numba, False)
        fresh = module._zero_preserving_modulo_inner_loop
        assert fresh is not plain
        assert not hasattr(fresh, "py_func")
        after = _modulo(compiled), _modulo(plain), _modulo(fresh)
        assert after[0] == before[0]
        assert after[1] == after[2] == before[1]
        outcomes.append((before, after))
    assert outcomes[1] == outcomes[0]


def test_actual_reload_old_outer_uses_current_inner_while_old_plain_stays_plain(
    implementations,
):
    original, current, numba, _ = implementations
    outcomes = []
    for module in (original, current):
        module = _reload(module, numba, False)
        old_inner = module._zero_preserving_modulo_inner_loop
        old_outer = module._zero_preserving_modulo_loop
        before = _modulo(old_inner), _outer(old_outer)
        module = _reload(module, numba, True)
        assert old_inner.__globals__ is vars(module)
        assert old_outer.__globals__ is vars(module)
        assert module._zero_preserving_modulo_inner_loop.py_func is not old_inner
        after = (_modulo(old_inner), _outer(old_outer),
                 _outer(module._zero_preserving_modulo_loop))
        assert after[0] == before[0]
        assert after[1] == after[2]
        outcomes.append((before, after))
    assert outcomes[1] == outcomes[0]


def test_actual_reload_retains_direct_compiled_alias_with_real_typed_mapping(
    implementations,
):
    original, current, numba, _ = implementations
    mapping = numba.typed.Dict.empty(key_type=numba.types.int64,
                                    value_type=numba.types.uint16)
    mapping[-2], mapping[7] = 5, 9
    data = np.array([-2, 999, 7], dtype=np.int64)
    outcomes = []
    for module in (original, current):
        module = _reload(module, numba, True)
        compiled = module._labels_raw_to_texture_direct_inner_loop
        out = np.full(data.shape, 33, dtype=np.uint16)
        assert compiled(data, mapping, out) is out
        before = out.tolist()
        module = _reload(module, numba, False)
        second = np.full(data.shape, 33, dtype=np.uint16)
        assert compiled(data, mapping, second) is second
        third = np.full(data.shape, 33, dtype=np.uint16)
        assert compiled.py_func(data, mapping, third) is third
        outcomes.append((before, second.tolist(), third.tolist()))
    assert outcomes[0] == ([5, 33, 9], [5, 33, 9], [5, 33, 9])
    assert outcomes[1] == outcomes[0]


def test_absent_numba_initialization_keeps_both_exact_plain_functions(implementations):
    original, current, numba, native = implementations
    outcomes = []
    for module, is_current in ((original, False), (current, True)):
        namespace, functions = _scratch(module, numba, native, current=is_current,
                                        enabled=False)
        outcomes.append(tuple(
            namespace[name] is function and not hasattr(namespace[name], "py_func")
            for name, function in zip(INNER_NAMES, functions, strict=True)
        ))
    assert outcomes[0] == (True, True)
    assert outcomes[1] == outcomes[0]


@pytest.mark.parametrize("retain_plain_alias", [False, True])
def test_dispatcher_namespace_cycle_and_caller_py_func_lifetime(
    implementations, retain_plain_alias,
):
    original, current, numba, native = implementations
    outcomes = []

    class Sentinel:
        pass

    for module, is_current in ((original, False), (current, True)):
        namespace, functions = _scratch(module, numba, native, current=is_current)
        namespace["_dispatcher_test_sentinel"] = Sentinel()
        sentinel = weakref.ref(namespace["_dispatcher_test_sentinel"])
        dispatcher = namespace[INNER_NAMES[0]]
        dispatcher_ref = weakref.ref(dispatcher)
        function_ref = weakref.ref(dispatcher.py_func)
        retained = dispatcher.py_func if retain_plain_alias else None
        del dispatcher, functions, namespace
        gc.collect()
        first = (sentinel() is not None, dispatcher_ref() is not None,
                 function_ref() is not None)
        del retained
        gc.collect()
        last = (sentinel() is None, dispatcher_ref() is None, function_ref() is None)
        outcomes.append((first, last))
    assert outcomes[0] == ((retain_plain_alias,) * 3, (True, True, True))
    assert outcomes[1] == outcomes[0]
