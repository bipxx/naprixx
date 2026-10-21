"""LayerList control against pinned bodies, with shared layer/container services."""

import ast
import gc
import importlib.util
import inspect
import sys
import types
import warnings
import weakref
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

from napari.components import LayerList
from napari.components import layerlist as current
from napari.layers import Image
from naparixx import _native

ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def original():
    name = "_layerlist_controller_pinned"
    path = ROOT / ".upstream/napari/src/napari/components/layerlist.py"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    # EventEmitter gives internal callbacks priority based on module metadata.
    module.LayerList.__module__ = current.__name__
    for value in vars(module.LayerList).values():
        if isinstance(value, (staticmethod, classmethod)):
            value = value.__func__
        if isinstance(value, types.FunctionType):
            value.__module__ = current.__name__
        elif isinstance(value, property):
            for function in (value.fget, value.fset):
                if function:
                    function.__module__ = current.__name__
    yield module
    sys.modules.pop(name, None)


def images():
    return [
        Image(np.zeros((3, 4)), name="a", units=("mm", "mm")),
        Image(np.ones((4, 5)), name="b", units=("um", "um")),
        Image(np.zeros((2, 6)), name="c", units=("mm", "mm")),
    ]


def snapshot(layers):
    extent = layers.extent
    return (
        tuple(layer.name for layer in layers),
        tuple(sorted(layer.name for layer in layers.selection)),
        tuple(layer.visible for layer in layers),
        tuple(tuple(row) for row in extent.world),
        tuple(extent.step),
        None if layers.units is None else tuple(map(str, layers.units)),
        layers.ndim,
        layers.axis_labels,
        layers._ranges,
    )


@pytest.mark.parametrize("operation", ("insert", "replace", "slice", "rename", "clear"))
def test_mutation_events_and_caches_match_pinned(original, operation):
    observations = []
    for cls in (LayerList, original.LayerList):
        layers = cls(images())
        events = []

        def record(event, events=events):
            events.append((event.type, getattr(event, "index", None)))

        layers.events.connect(record)
        before = layers.extent
        if operation == "insert":
            layers.insert(-1, Image(np.zeros((3, 8)), name="a"))
        elif operation == "replace":
            layers[1] = Image(np.zeros((9, 10)), name="new")
        elif operation == "slice":
            layers[1:] = [Image(np.zeros((2, 7)), name="new")]
        elif operation == "rename":
            layers[0].name = "b"
        else:
            layers.clear()
        observations.append((snapshot(layers), events, before is layers.extent))
    assert observations[0] == observations[1]


@pytest.mark.parametrize(
    "locked", ((), (False,), (True,), (False, True, False), (True, True, True))
)
def test_selected_removal_and_lock_notification_match(original, monkeypatch, locked):
    import napari.utils.notifications as notifications

    observations = []
    for cls in (LayerList, original.LayerList):
        messages = []
        monkeypatch.setattr(notifications, "show_info", messages.append)
        layers = cls(images()[: len(locked)])
        layers.selection.update(layers)
        for layer, lock in zip(layers, locked, strict=True):
            layer.locked = lock
        layers.remove_selected()
        observations.append((snapshot(layers), messages))
    assert observations[0] == observations[1]


def test_visibility_linking_and_unlinking_match(original):
    observations = []
    for cls in (LayerList, original.LayerList):
        layers = cls(images())
        layers.selection.update((layers[0], layers[2]))
        layers.toggle_selected_visibility()
        visibility = tuple(layer.visible for layer in layers)
        layers.link_layers(("a", layers[1]), ("opacity",))
        layers[0].opacity = 0.4
        linked = tuple(layer.opacity for layer in layers)
        layers.unlink_layers(("a", "b"), ("opacity",))
        layers[0].opacity = 0.7
        observations.append(
            (visibility, linked, tuple(layer.opacity for layer in layers))
        )
    assert observations[0] == observations[1]


@pytest.mark.parametrize("units", (None, ("m", "m"), ("s", "s"), ("mm",)))
def test_units_transitions_and_errors_match(original, units):
    observations = []
    for cls in (LayerList, original.LayerList):
        layers = cls(images())
        events = []
        layers.events.units.connect(
            lambda event, events=events: events.append(
                tuple(map(str, event.value)) if event.value is not None else None
            )
        )
        try:
            layers.units = units
            outcome = None
        except ValueError as error:
            outcome = type(error).__name__, str(error)
        observations.append((snapshot(layers), events, outcome))
    assert observations[0] == observations[1]


@pytest.mark.parametrize("action", ("insert", "data"))
def test_dimension_growth_drops_units_override(original, action):
    observations = []
    for cls in (LayerList, original.LayerList):
        layers = cls(images())
        layers.units = ("m", "m")
        with warnings.catch_warnings(record=True) as records:
            warnings.simplefilter("always")
            if action == "insert":
                layers.append(Image(np.zeros((2, 3, 4))))
            else:
                layers[0].data = np.zeros((2, 3, 4))
        observations.append(
            (
                layers._units,
                [(record.category.__name__, str(record.message)) for record in records],
            )
        )
    assert observations[0] == observations[1]


@pytest.mark.parametrize("augmented", (False, True))
@pytest.mark.parametrize("populated", (False, True))
def test_extent_step_and_subset_calculations_match(original, augmented, populated):
    observations = []
    for cls in (LayerList, original.LayerList):
        layers = cls(images() if populated else ())
        extents = [layer.extent for layer in layers]
        world = layers._get_extent_world(
            extents, augmented=augmented, units=layers.units
        )
        subset = layers.get_extent(iter(list(layers)[::2]))
        observations.append(
            (
                world,
                layers._get_step_size(extents, units=layers.units),
                subset.world,
                subset.step,
            )
        )
    for left, right in zip(*observations, strict=True):
        np.testing.assert_allclose(left, right)


@pytest.mark.parametrize(
    "labels", ((None, None), (("x", "y"), ("u", "v")), (("a", "b", "c"), ("x", "y")))
)
def test_annotation_ties_and_padding_match(original, labels):
    observations = []
    for cls in (LayerList, original.LayerList):
        layers = []
        for axis_labels in labels:
            ndim = len(axis_labels) if axis_labels is not None else 2
            layers.append(Image(np.zeros((2,) * ndim), axis_labels=axis_labels))
        model = cls(layers)
        observations.append(
            (model.axis_labels, model._find_longest_annotated(), model.ndim)
        )
    assert observations[0] == observations[1]


@pytest.mark.parametrize("selected", (False, True))
@pytest.mark.parametrize("populated", (False, True))
def test_save_dispatch_and_warnings_match(original, monkeypatch, selected, populated):
    import napari.plugins.io as io

    observations = []
    for cls in (LayerList, original.LayerList):
        calls = []

        def save(path, layers, *, plugin, _writer, calls=calls):
            calls.append((path, tuple(layer.name for layer in layers), plugin, _writer))
            return ["written"]

        monkeypatch.setattr(io, "save_layers", save)
        layers = cls(images() if populated else ())
        layers.selection.clear()
        if populated:
            layers.selection.add(layers[1])
        with warnings.catch_warnings(record=True) as records:
            warnings.simplefilter("always")
            result = layers.save(
                "target", selected=selected, plugin="plugin", _writer="writer"
            )
        observations.append(
            (result, calls, [str(record.message) for record in records])
        )
    assert observations[0] == observations[1]


@pytest.mark.parametrize(
    "phase", ("normal", "body", "begin", "end", "begin_and_end", "close")
)
def test_batching_cleanup_and_exception_context_match(original, phase):
    observations = []
    for cls in (LayerList, original.LayerList):
        calls = []

        def begin(calls=calls):
            calls.append(
                ("begin", type(sys.exception()).__name__ if sys.exception() else None)
            )
            if phase in ("begin", "begin_and_end"):
                raise ValueError("begin failed")

        def end(calls=calls):
            calls.append(
                ("end", type(sys.exception()).__name__ if sys.exception() else None)
            )
            if phase in ("end", "begin_and_end"):
                raise KeyError("end failed")

        model = SimpleNamespace(
            events=SimpleNamespace(begin_batch=begin, end_batch=end)
        )
        try:
            if phase == "close":
                generator = cls.batched_update.__wrapped__(model)
                assert next(generator) is None
                generator.close()
            else:
                with cls.batched_update(model):
                    if phase == "body":
                        raise RuntimeError("body failed")
            error = None
        except BaseException as exception:
            error = (
                type(exception).__name__,
                str(exception),
                type(exception.__context__).__name__ if exception.__context__ else None,
            )
        observations.append((calls, error))
    assert observations[0] == observations[1]


@pytest.mark.parametrize("property_name", ("ndim", "_ranges"))
def test_lazy_provider_arguments_keep_python_generator_protocols(
    original, monkeypatch, property_name
):
    observations = []
    for module in (current, original):
        calls, captured = [], []

        class Layers:
            def __iter__(self, calls=calls):
                calls.append("iterate")
                return iter([SimpleNamespace(ndim=4)])

        def consume(values, calls=calls, captured=captured, **kwargs):
            assert inspect.isgenerator(values)
            captured.append(values)
            calls.append("factory")
            return "deferred"

        name = "max" if property_name == "ndim" else "tuple"
        monkeypatch.setattr(module, name, consume, raising=False)
        target = (
            Layers()
            if property_name == "ndim"
            else SimpleNamespace(
                extent=SimpleNamespace(world=np.array([[0], [2]]), step=(1,))
            )
        )
        result = getattr(module.LayerList, property_name).fget(target)
        assert result == "deferred"
        assert next(captured[0]) == (
            4 if property_name == "ndim" else current.RangeTuple(0, 2, 1)
        )
        captured[0].close()
        observations.append(calls)
        monkeypatch.delattr(module, name)
    assert observations[0] == observations[1]


@pytest.mark.parametrize(
    "record_name", ("LayerListValueIterator", "LayerListBatchContext")
)
def test_uninitialized_native_protocol_objects_collect(record_name):
    cls = getattr(_native, record_name)
    value = cls.__new__(cls)
    assert any(reference is cls for reference in gc.get_referents(value))
    reference = weakref.ref(value)
    del value
    gc.collect()
    assert reference() is None


def test_native_batch_model_edge_and_public_generator_cycles_collect():
    layers = LayerList(images())
    context = _native.layerlist_batch_context(layers)
    assert any(value is layers for value in gc.get_referents(context))
    references = weakref.ref(layers), weakref.ref(context)
    layers.native_context = context
    generator = layers.batched_update()
    generator.__enter__()
    layers.generator = generator
    del context, generator, layers
    gc.collect()
    assert all(reference() is None for reference in references)


def test_every_original_signature_doc_and_decorator_is_preserved():
    original = ast.parse(
        (
            ROOT
            / ".artifacts/layerlist-control-prior-src__napari__components__layerlist.py"
        ).read_text(encoding="utf-8")
    )
    current_tree = ast.parse(Path(current.__file__).read_text(encoding="utf-8"))

    def records(tree):
        result = []
        for node in tree.body:
            nodes = node.body if isinstance(node, ast.ClassDef) else [node]
            for function in nodes:
                if (
                    isinstance(function, ast.FunctionDef)
                    and function.name != "_layerlist_yield_from"
                ):
                    result.append(
                        (
                            function.name,
                            ast.dump(function.args, include_attributes=False),
                            [
                                ast.dump(d, include_attributes=False)
                                for d in function.decorator_list
                            ],
                            ast.get_docstring(function, clean=False),
                            ast.dump(function.returns, include_attributes=False)
                            if function.returns
                            else None,
                        )
                    )
        return result

    assert records(original) == records(current_tree)
