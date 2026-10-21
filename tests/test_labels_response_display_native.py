"""Missing Labels response-display contracts; exact whole-body oracle is test-only.

Production never imports this file. Root alone runs its finite paired cohort.
Existing display/publication/actual Qt oracles remain unchanged and are reused.
"""
from __future__ import annotations

import ast
import builtins
import gc
import hashlib
import importlib
import inspect
import types
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".upstream/napari/src/napari"
PINNED = {
    "layers/labels/labels.py": "8d5ac1e119018af7b49df29838a91273910df09c2d4e4e6ec7e58a3744e7a176",
    "layers/labels/_labels_utils.py": "0fa4ebdb8d9d6664b405387730267153fbce79ebad94be548a04752f9ad18130",
}
OWNED = ("_calculate_contour", "_raw_to_displayed")
NATIVE_EXPORTS = (
    "labels_response_raw_locals", "labels_response_contour_locals",
    "labels_response_morphology_locals", "labels_response_raw_to_displayed",
    "labels_response_calculate_contour", "labels_response_get_contours",
)


def _nodes(path, owner, names, pinned=False):
    raw = path.read_bytes()
    if pinned:
        assert hashlib.sha256(raw).hexdigest() == PINNED[path.relative_to(UPSTREAM).as_posix()]
    tree = ast.parse(raw.decode("utf-8"))
    members = tree.body if owner is None else next(
        n.body for n in tree.body if isinstance(n, ast.ClassDef) and n.name == owner
    )
    selected = [next(n for n in reversed(members)
                     if isinstance(n, ast.FunctionDef) and n.name == name)
                for name in names]
    future = [n for n in tree.body if isinstance(n, ast.ImportFrom) and n.module == "__future__"]
    return future, selected


def _compile(path, owner, names, namespace, pinned=False):
    future, selected = _nodes(path, owner, names, pinned)
    if owner is None:
        body = selected
    else:
        # Full selected source methods keep original names/decorators/class scope.
        cls = ast.parse("class Labels:\n    pass\n").body[0]
        cls.body = selected
        body = [cls]
    module = ast.fix_missing_locations(ast.Module(body=future + body, type_ignores=[]))
    exec(compile(module, str(path), "exec", dont_inherit=True), namespace)
    return selected


@pytest.fixture(scope="module")
def implementations():
    labels_module = importlib.import_module("napari.layers.labels.labels")
    utils_module = importlib.import_module("napari.layers.labels._labels_utils")
    pair = []
    for pinned in (True, False):
        label_ns, utils_ns = dict(vars(labels_module)), dict(vars(utils_module))
        label_builtins, utils_builtins = dict(vars(builtins)), dict(vars(builtins))
        label_ns["__builtins__"], utils_ns["__builtins__"] = label_builtins, utils_builtins
        base = UPSTREAM if pinned else ROOT / "src/napari"
        _compile(base / "layers/labels/_labels_utils.py", None,
                 ("get_contours",), utils_ns, pinned)
        label_ns["get_contours"] = utils_ns["get_contours"]
        _compile(base / "layers/labels/labels.py", "Labels", OWNED, label_ns, pinned)
        cls = label_ns["Labels"]
        pair.append(SimpleNamespace(
            label_ns=label_ns, utils_ns=utils_ns,
            label_builtins=label_builtins, utils_builtins=utils_builtins,
            cls=cls, raw=cls._raw_to_displayed, contour=cls._calculate_contour,
            contours=utils_ns["get_contours"], pinned=pinned,
        ))
    assert pair[0].contours is not pair[1].contours
    for impl in pair:
        assert impl.raw.__globals__ is impl.label_ns
        assert impl.contour.__globals__["get_contours"] is impl.contours
        assert impl.contours.__globals__ is impl.utils_ns
    current_names = {n.func.attr for n in ast.walk(_nodes(
        ROOT / "src/napari/layers/labels/labels.py", "Labels", OWNED)[1][0])
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)}
    if "labels_response_calculate_contour" in current_names:
        native = importlib.import_module("naparixx")._native
        for name in NATIVE_EXPORTS:
            assert callable(getattr(native, name))
        for name in ("LabelsResponseRawLocals", "LabelsResponseContourLocals",
                     "LabelsResponseMorphologyLocals", "LabelsResponseSliceIterator"):
            assert isinstance(getattr(native, name), type)
    return tuple(pair)


def _compare(implementations, scenario, contract):
    expected = scenario(implementations[0])
    assert expected == contract
    assert scenario(implementations[1]) == expected


def _error(call):
    try:
        call()
    except Exception as error:
        return type(error).__name__, str(error)
    raise AssertionError("expected source failure")


def _slices(value):
    return tuple((s.start, s.stop, s.step) for s in value)


def test_exact_three_source_metadata_and_executing_namespaces(implementations):
    original, current = implementations
    for left, right in ((original.raw, current.raw), (original.contour, current.contour),
                        (original.contours, current.contours)):
        assert inspect.signature(left) == inspect.signature(right)
        assert left.__doc__ == right.__doc__
        assert left.__name__ == right.__name__
        assert left.__qualname__ == right.__qualname__
        assert inspect.isgeneratorfunction(left) == inspect.isgeneratorfunction(right)
        assert left.__globals__ is not right.__globals__


def test_contour_captures_max_int_expand_before_reentrant_argument_reads(implementations, monkeypatch):
    def scenario(impl):
        log = []
        expanded = (slice(0, 3),)
        output = object()
        def decoy(*args, **kwargs):
            raise AssertionError("reloaded captured source callable")
        def as_int(value):
            log.append(("int", value))
            return value
        def maximum(a, b):
            log.append(("max", a, b))
            return max(a, b)
        def expand(key, shape, offset):
            log.append(("expand", key[0].start, shape, offset))
            return expanded
        class Layer:
            reads = 0
            colormap = SimpleNamespace(background_value=0)
            @property
            def contour(self):
                self.reads += 1
                log.append(("contour", self.reads))
                if self.reads == 2:
                    impl.label_ns["int"] = impl.label_ns["max"] = decoy
                return 1
        class Labels:
            ndim = 2
            @property
            def shape(self):
                log.append("shape")
                impl.label_ns["expand_slice"] = decoy
                return (3,)
            def __getitem__(self, key):
                log.append("select")
                return self
        class Contours:
            def __getitem__(self, key):
                log.append(("crop", _slices(key)))
                return output
        with monkeypatch.context() as patch:
            patch.setitem(impl.label_ns, "max", maximum)
            patch.setitem(impl.label_ns, "int", as_int)
            patch.setitem(impl.label_ns, "expand_slice", expand)
            patch.setitem(impl.label_ns, "get_contours", lambda *args: Contours())
            result = impl.contour(Layer(), Labels(), (slice(1, 3),))
        return tuple(log), result is output
    _compare(implementations, scenario, (
        (("contour", 1), ("contour", 2), ("int", 1), ("max", 1, 1),
         "shape", ("expand", 1, (3,), 1), "select", ("contour", 3),
         ("crop", ((1, 3, None),))), True,
    ))


@pytest.mark.parametrize("failure", [None, "shape_iter", "slice_stop"])
def test_raw_tuple_consumer_captures_before_shape_and_yields_fresh_slice(implementations, monkeypatch, failure):
    def scenario(impl):
        log = []
        sentinel = object()
        def decoy(*args):
            raise AssertionError("tuple consumer reloaded after shape")
        def second_slice(start, size):
            log.append(("slice2", start, size))
            if failure == "slice_stop":
                raise StopIteration("slice callback")
            return slice(start, size)
        def first_slice(start, size):
            log.append(("slice1", start, size))
            impl.label_ns["slice"] = second_slice
            return slice(start, size)
        class Shape:
            def __iter__(self):
                log.append("shape-iter")
                if failure == "shape_iter":
                    raise RuntimeError("shape iteration failed")
                return iter((2, 4))
        class Raw:
            @property
            def shape(self):
                log.append("shape")
                impl.label_ns["tuple"] = decoy
                return Shape()
        def consume(generator):
            log.append(("tuple", isinstance(generator, types.GeneratorType),
                        generator.__name__, generator.__qualname__))
            return tuple(generator)
        class Layer:
            def _calculate_contour(self, labels, key):
                log.append(("override", _slices(key)))
                return sentinel
            colormap = SimpleNamespace(_data_to_texture=lambda value: value)
        with monkeypatch.context() as patch:
            patch.setitem(impl.label_ns, "tuple", consume)
            patch.setitem(impl.label_ns, "slice", first_slice)
            if failure is None:
                outcome = impl.raw(Layer(), Raw()) is sentinel
            else:
                outcome = _error(lambda: impl.raw(Layer(), Raw()))
        return tuple(log), outcome
    prefix = ("shape", "shape-iter")
    if failure == "shape_iter":
        expected = prefix, ("RuntimeError", "shape iteration failed")
    else:
        prefix += (("tuple", True, "<genexpr>", "Labels._raw_to_displayed.<locals>.<genexpr>"),
                   ("slice1", 0, 2), ("slice2", 0, 4))
        expected = (prefix + (("override", ((0, 2, None), (0, 4, None))),), True)
        if failure == "slice_stop":
            expected = prefix, ("RuntimeError", "generator raised StopIteration")
    _compare(implementations, scenario, expected)


@pytest.mark.parametrize("case", ["success", "pair_short", "slice_stop"])
def test_delta_tuple_captures_zip_and_preserves_lazy_pair_protocol(implementations, monkeypatch, case):
    def scenario(impl):
        log = []
        first, second = slice(2, 5), slice(1, 6)
        output = object()
        def decoy(*args, **kwargs):
            raise AssertionError("captured tuple was reloaded")
        def pairs(left, right, *, strict):
            log.append(("zip", strict))
            impl.label_ns["tuple"] = decoy
            return iter(((first,) if case == "pair_short" else (first, second),))
        def make_slice(start, stop):
            log.append(("slice", start, stop))
            if case == "slice_stop":
                raise StopIteration("delta slice callback")
            return slice(start, stop)
        def consume(generator):
            log.append(("tuple", isinstance(generator, types.GeneratorType),
                        generator.__name__, generator.__qualname__))
            return tuple(generator)
        class Labels:
            ndim, shape = 2, (7,)
            def __getitem__(self, key):
                return self
        class Contours:
            def __getitem__(self, key):
                log.append(("crop", _slices(key)))
                return output
        layer = SimpleNamespace(contour=1, colormap=SimpleNamespace(background_value=0))
        with monkeypatch.context() as patch:
            patch.setitem(impl.label_ns, "expand_slice", lambda *args: (second,))
            patch.setitem(impl.label_ns, "get_contours", lambda *args: Contours())
            patch.setitem(impl.label_ns, "tuple", consume)
            patch.setitem(impl.label_ns, "zip", pairs)
            patch.setitem(impl.label_ns, "slice", make_slice)
            outcome = (impl.contour(layer, Labels(), (first,)) is output if case == "success"
                       else _error(lambda: impl.contour(layer, Labels(), (first,))))
        return tuple(log), outcome
    prefix = (("zip", False), ("tuple", True, "<genexpr>",
              "Labels._calculate_contour.<locals>.<genexpr>"))
    if case == "pair_short":
        expected = prefix, ("ValueError", "not enough values to unpack (expected 2, got 1)")
    elif case == "slice_stop":
        expected = prefix + (("slice", 1, 4),), ("RuntimeError", "generator raised StopIteration")
    else:
        expected = prefix + (("slice", 1, 4), ("crop", ((1, 4, None),))), True
    _compare(implementations, scenario, expected)


def _simple_backend(log):
    structure, thick, eroded, mask, output = object(), object(), object(), object(), object()
    class Footprint:
        def astype(self, dtype):
            log.append(("astype", dtype is bool))
            return thick
    class Dilated:
        def __eq__(self, other):
            log.append(("compare", other is eroded))
            return mask
    class Output:
        def __setitem__(self, index, value):
            log.append(("fill", index is mask, value))
    output = Output()
    class Backend:
        def generate_binary_structure(self, ndim, connectivity):
            log.append(("structure", ndim, connectivity))
            return structure
        def iterate_structure(self, value, thickness):
            log.append(("iterate", value is structure, thickness))
            return Footprint()
        def grey_dilation(self, labels, *, footprint):
            log.append(("dilate", footprint is structure))
            return Dilated()
        def grey_erosion(self, labels, *, footprint):
            log.append(("erode", footprint is thick))
            return eroded
    class Labels:
        @property
        def ndim(self):
            log.append("ndim")
            return 2
        def copy(self):
            log.append("copy")
            return output
    return Backend(), Labels(), output


@pytest.mark.parametrize("failure", [None, "import", "from", "missing_importer"])
def test_morphology_uses_function_builtins_local_scipy_import_before_attributes(implementations, monkeypatch, failure):
    def scenario(impl):
        log = []
        backend, labels, output = _simple_backend(log)
        class Imported:
            @property
            def ndimage(self):
                log.append("from-ndimage")
                if failure == "from":
                    raise RuntimeError("ndimage lookup failed")
                return backend
        def importer(name, globals=None, locals=None, fromlist=(), level=0):
            log.append(("import", name, globals is impl.utils_ns, fromlist, level))
            if failure == "import":
                raise RuntimeError("scipy import failed")
            return Imported()
        def global_decoy(*args, **kwargs):
            raise AssertionError("global __import__ used instead of builtins")
        with monkeypatch.context() as patch:
            patch.setitem(impl.utils_ns, "__import__", global_decoy)
            if failure == "missing_importer":
                patch.delitem(impl.utils_builtins, "__import__")
            else:
                patch.setitem(impl.utils_builtins, "__import__", importer)
            outcome = (impl.contours(labels, 2, 5) is output if failure is None
                       else _error(lambda: impl.contours(labels, 2, 5)))
        return tuple(log), outcome
    prefix = (("import", "scipy", True, ("ndimage",), 0),)
    if failure == "import":
        expected = prefix, ("RuntimeError", "scipy import failed")
    elif failure == "from":
        expected = prefix + ("from-ndimage",), ("RuntimeError", "ndimage lookup failed")
    elif failure == "missing_importer":
        expected = (), ("ImportError", "__import__ not found")
    else:
        expected = prefix + ("from-ndimage", "ndim", ("structure", 2, 1),
                             ("iterate", True, 2), ("astype", True), ("dilate", True),
                             ("erode", True), ("compare", True), "copy", ("fill", True, 5)), True
    _compare(implementations, scenario, expected)


def test_morphology_captures_imported_receiver_factory_and_fresh_bool_argument(implementations, monkeypatch):
    def scenario(impl):
        log = []
        structure, thick, erased, mask, output, dtype = (object() for _ in range(6))
        def decoy(*args, **kwargs):
            raise AssertionError("captured morphology receiver/callable reloaded")
        class Footprint:
            @property
            def astype(self):
                log.append("astype-lookup")
                impl.utils_ns["bool"] = dtype
                return lambda value: (log.append(("astype", value is dtype)), thick)[1]
        class Dilated:
            def __eq__(self, other):
                return mask
        class Output:
            def __setitem__(self, key, value):
                pass
        output = Output()
        class Backend:
            generate = None
            @property
            def generate_binary_structure(self):
                log.append("generate-lookup")
                return self.generate
            def iterate_structure(self, value, amount):
                log.append(("iterate", value is structure, amount))
                return Footprint()
            def grey_dilation(self, labels, *, footprint):
                return Dilated()
            def grey_erosion(self, labels, *, footprint):
                return erased
        backend = Backend()
        backend.generate = lambda rank, conn: (log.append(("generate", rank, conn)), structure)[1]
        class Labels:
            @property
            def ndim(self):
                log.append("ndim")
                backend.generate = decoy
                imported.ndimage = SimpleNamespace(iterate_structure=decoy)
                return 2
            def copy(self):
                return output
        imported = SimpleNamespace(ndimage=backend)
        def importer(*args):
            log.append("import")
            return imported
        with monkeypatch.context() as patch:
            patch.setitem(impl.utils_ns, "bool", bool)
            patch.setitem(impl.utils_builtins, "__import__", importer)
            result = impl.contours(Labels(), 3, 0)
        return tuple(log), result is output
    _compare(implementations, scenario, (
        ("import", "generate-lookup", "ndim", ("generate", 2, 1),
         ("iterate", True, 3), "astype-lookup", ("astype", True)), True,
    ))


@pytest.mark.parametrize("failure", [None, "erosion", "copy", "fill"])
def test_morphology_assigned_locals_have_source_error_and_normal_lifetimes(implementations, monkeypatch, failure):
    def scenario(impl):
        refs = {}
        class Token:
            def __init__(self, name):
                self.name = name
                refs[name] = weakref.ref(self)
            def astype(self, dtype):
                return Token("thick_struct_elem")
            def __eq__(self, other):
                return Token("not_boundaries")
            def __setitem__(self, key, value):
                if failure == "fill":
                    raise RuntimeError("fill failed")
        class Backend:
            def __init__(self):
                refs["ndi"] = weakref.ref(self)
            def generate_binary_structure(self, rank, conn):
                return Token("struct_elem")
            def iterate_structure(self, value, thickness):
                return Token("astype_receiver")
            def grey_dilation(self, labels, *, footprint):
                return Token("dilated_labels")
            def grey_erosion(self, labels, *, footprint):
                if failure == "erosion":
                    raise RuntimeError("erosion failed")
                return Token("eroded_labels")
        class Labels:
            ndim = 2
            def copy(self):
                if failure == "copy":
                    raise RuntimeError("copy failed")
                return Token("contours")
        def importer(*args):
            return SimpleNamespace(ndimage=Backend())
        with monkeypatch.context() as patch:
            patch.setitem(impl.utils_builtins, "__import__", importer)
            saved = None
            if failure is None:
                result = impl.contours(Labels(), 2, 0)
            else:
                try:
                    impl.contours(Labels(), 2, 0)
                except RuntimeError as error:
                    message, saved = str(error), error.__traceback__
                else:
                    raise AssertionError("expected morphology failure")
            live = tuple(name for name, ref in refs.items() if ref() is not None)
            if failure is None:
                del result
                message = None
            del saved
            gc.collect()
            released = all(ref() is None for ref in refs.values())
        return live, message, released
    if failure is None:
        contract = ("contours",), None, True
    else:
        names = ("ndi", "struct_elem", "thick_struct_elem", "dilated_labels")
        if failure in ("copy", "fill"):
            names += ("eroded_labels", "not_boundaries")
        if failure == "fill":
            names += ("contours",)
        contract = names, failure + " failed", True
    _compare(implementations, scenario, contract)


@pytest.mark.parametrize("failure", [False, True])
def test_raw_assigned_slice_and_selected_locals_survive_only_saved_source_error(implementations, monkeypatch, failure):
    def scenario(impl):
        refs = {}
        # A shadowed tuple may return any provider object; use weakrefable list.
        class SliceList(list):
            pass
        class Selected:
            pass
        class Raw:
            shape = (2,)
            def __getitem__(self, key):
                value = Selected()
                refs["sliced_labels"] = weakref.ref(value)
                return value
        def consumer(generator):
            value = SliceList(generator)
            refs["data_slice"] = weakref.ref(value)
            return value
        def texture(value):
            if failure:
                raise RuntimeError("texture failed")
            return object()
        layer = SimpleNamespace(_calculate_contour=lambda labels, key: None,
                                colormap=SimpleNamespace(_data_to_texture=texture))
        with monkeypatch.context() as patch:
            patch.setitem(impl.label_ns, "tuple", consumer)
            saved = None
            if failure:
                try:
                    impl.raw(layer, Raw())
                except RuntimeError as error:
                    saved = error.__traceback__
                else:
                    raise AssertionError("expected texture failure")
            else:
                impl.raw(layer, Raw())
            retained = tuple(name for name, ref in refs.items() if ref() is not None)
            del saved
            gc.collect()
            released = all(ref() is None for ref in refs.values())
        return retained, released
    _compare(implementations, scenario, (
        (("data_slice", "sliced_labels") if failure else ()), True,
    ))


@pytest.mark.parametrize("metadata", ["name", "file"])
def test_morphology_import_from_preserves_metadata_getter_errors(implementations, monkeypatch, metadata):
    import sys
    def scenario(impl):
        reads = []
        module_name = "_naparixx_labels_response_missing_metadata_probe"
        class Imported:
            __spec__ = None
            @property
            def ndimage(self):
                raise AttributeError("missing ndimage")
            @property
            def __name__(self):
                if metadata == "name":
                    raise LookupError("module name lookup failed")
                return module_name
            @property
            def __file__(self):
                reads.append("__file__")
                raise LookupError("module file lookup failed")
        class Labels:
            @property
            def ndim(self):
                reads.append("ndim")
                raise AssertionError("labels read after failed IMPORT_FROM")
        def importer(name, globals=None, locals=None, fromlist=(), level=0):
            assert name == "scipy" and globals is impl.utils_ns
            assert fromlist == ("ndimage",) and level == 0
            return Imported()
        with monkeypatch.context() as patch:
            patch.delitem(sys.modules, module_name, raising=False)
            patch.delitem(sys.modules, module_name + ".ndimage", raising=False)
            patch.setitem(impl.utils_builtins, "__import__", importer)
            outcome = _error(lambda: impl.contours(Labels(), 2, 0))
        return outcome, tuple(reads)
    source_name = ("<unknown module name>" if metadata == "name"
                   else "_naparixx_labels_response_missing_metadata_probe")
    if metadata == "name" and sys.version_info >= (3, 13):
        error_contract = ("LookupError", "module name lookup failed")
    else:
        error_contract = ("ImportError", "cannot import name 'ndimage' from '"
                          + source_name + "' (unknown location)")
    _compare(implementations, scenario, (error_contract, ()))



def test_warning_literal_identity_is_retained_across_same_function_calls(implementations, monkeypatch):
    def scenario(impl):
        messages = []
        layer = SimpleNamespace(contour=1)
        labels = SimpleNamespace(ndim=3)
        with monkeypatch.context() as patch:
            patch.setitem(impl.label_ns, "warnings", SimpleNamespace(warn=messages.append))
            first = impl.contour(layer, labels, ())
            second = impl.contour(layer, labels, ())
        assert len(messages) == 2
        return (first is None, second is None,
                messages[0] == "Contours are not displayed during 3D rendering",
                messages[0] is messages[1])
    _compare(implementations, scenario, (True, True, True, True))


def test_import_literal_identities_are_retained_across_same_function_calls(implementations, monkeypatch):
    def scenario(impl):
        received = []
        backend, labels, output = _simple_backend([])
        def importer(name, globals=None, locals=None, fromlist=(), level=0):
            assert name == "scipy" and fromlist == ("ndimage",) and level == 0
            received.append((name, fromlist, globals))
            return SimpleNamespace(ndimage=backend)
        with monkeypatch.context() as patch:
            patch.setitem(impl.utils_builtins, "__import__", importer)
            first = impl.contours(labels, 1, 0)
            second = impl.contours(labels, 1, 0)
        assert len(received) == 2
        return (first is output, second is output,
                received[0][2] is impl.utils_ns, received[1][2] is impl.utils_ns,
                received[0][0] is received[1][0], received[0][1] is received[1][1],
                received[0][1][0] is received[1][1][0])
    _compare(implementations, scenario, (True, True, True, True, True, True, True))
