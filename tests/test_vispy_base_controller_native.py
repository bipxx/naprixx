"""Finite Base20/canonical-namespace contracts against independent pinned bodies.

Both hierarchies are compiled from their own complete Base class source; the
original is an explicit oracle and is never mounted as application code. The
original disconnect_events body and blending dictionary are rebuilt separately.
NumPy, Pint, MatrixTransform and the installed event engine are real dependencies.
Models, sliced transform inputs, GL-capacity queries and visual publication are
controlled boundaries. These cases do not prove real-plugin layouts, arbitrary
reflection, actual GPU rendering, whole-model authority or clean GUI shutdown.
Native symbols are supplied only to the current hierarchy at test execution.
"""

from __future__ import annotations

import ast
import copy
import gc
import hashlib
import inspect
import re
import weakref
from abc import ABC, abstractmethod
from pathlib import Path
from types import SimpleNamespace
from typing import Generic, Literal, TypeAlias, TypeVar, cast

import numpy as np
import pint
import pytest
from vispy.visuals.transforms import MatrixTransform

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
PINNED_HASHES = {
    "_vispy/layers/base.py": (
        "77c7724baf17aac4ff90c7abcdf2b86284dc85c2220cdc0bc140695a2de1e52f"
    ),
    "utils/events/event_utils.py": (
        "a4429354e63bc47e998f817c371d17ef4cbc13c32b18b80673c785335793e95e"
    ),
    "_vispy/utils/gl.py": (
        "76278bb2e622cc723ef8cbfc7302b9cba0869520e68475c126aa302c6641f65e"
    ),
}

# Existing actual original selectors accompany this bounded source comparison.
# Parameter expansions and runtime outcomes belong to root's acceptance runner.
ORIGINAL_SELECTORS = {
    "image": (
        "test_base_create",
        "test_3d_slice_of_2d_image_with_order",
        "test_2d_slice_of_3d_image_with_order",
        "test_transforming_child_node",
        "test_transforming_child_node_pyramid",
        "test_3d_multiscale_half_voxel_uses_rendered_level",
    ),
    "labels": (
        "test_colormap_rebuilt_when_slice_dtypes_change",
        "test_colormap_not_rebuilt_when_slice_dtypes_are_unchanged",
    ),
    "canvas": (
        "test_layer_overlays",
        "test_world_units_restored_after_removing_inconsistent_layer",
        "test_world_units_applied_to_inserted_layer_via_layerlist_event",
        "test_inserted_layer_receives_shared_world_units_when_units_unchanged",
    ),
}
EVENT_CALLBACKS = (
    ("refresh", "_on_refresh_change"),
    ("set_data", "_on_data_change"),
    ("visible", "_on_visible_change"),
    ("opacity", "_on_opacity_change"),
    ("blending", "_on_blending_change"),
    ("scale", "_on_matrix_change"),
    ("translate", "_on_matrix_change"),
    ("rotate", "_on_matrix_change"),
    ("shear", "_on_matrix_change"),
    ("affine", "_on_matrix_change"),
    ("units", "_recalculate_units_scale"),
)


def _source(root, relative):
    raw = (root / relative).read_bytes()
    if root == PINNED:
        assert hashlib.sha256(raw).hexdigest() == PINNED_HASHES[relative]
    return raw.decode("utf-8")


def _segment(source, node):
    first = min([node.lineno, *(d.lineno for d in getattr(node, "decorator_list", ()))])
    text = "".join(source.splitlines(keepends=True)[first - 1 : node.end_lineno])
    assert ast.dump(ast.parse(text).body[0]) == ast.dump(node)
    return text


def _named(source, kind, name):
    return next(
        node
        for node in ast.parse(source).body
        if isinstance(node, kind)
        and (
            getattr(node, "name", None) == name
            or isinstance(node, ast.Assign)
            and any(isinstance(t, ast.Name) and t.id == name for t in node.targets)
            or isinstance(node, ast.AnnAssign)
            and isinstance(node.target, ast.Name)
            and node.target.id == name
        )
    )


@pytest.fixture(scope="module")
def implementations():
    # Root alone executes these installed dependencies in the frozen runner.
    from napari.utils.events import EmitterGroup, Event
    from naparixx import _native

    def build(root):
        trace = []
        model_type = type("ControlledLayer", (), {})
        state = SimpleNamespace(limits=(2048, 512), query_error=None)

        def capacities():
            trace.append(("capacities",))
            if state.query_error is not None:
                raise state.query_error
            return state.limits

        namespace = {
            "__name__": "_vispy_base_controller_contract",
            "ABC": ABC,
            "abstractmethod": abstractmethod,
            "Generic": Generic,
            "Literal": Literal,
            "TypeAlias": TypeAlias,
            "TypeVar": TypeVar,
            "cast": cast,
            "np": np,
            "pint": pint,
            "MatrixTransform": MatrixTransform,
            "Layer": model_type,
            "get_max_texture_sizes": capacities,
        }
        if root == CURRENT:
            namespace["_native"] = _native
        helpers = _source(root, "utils/events/event_utils.py")
        gl_source = _source(root, "_vispy/utils/gl.py")
        base_source = _source(root, "_vispy/layers/base.py")
        pieces = [
            _segment(helpers, _named(helpers, ast.FunctionDef, "disconnect_events")),
            _segment(gl_source, _named(gl_source, ast.Assign, "BLENDING_MODES")),
            _segment(base_source, _named(base_source, ast.Assign, "_L")),
            _segment(base_source, _named(base_source, ast.AnnAssign, "Vector3")),
            _segment(base_source, _named(base_source, ast.ClassDef, "VispyBaseLayer")),
        ]
        exec(
            compile(
                "from __future__ import annotations\n" + "\n\n".join(pieces),
                str(root / "_vispy/layers/base.py"),
                "exec",
            ),
            namespace,
        )
        return SimpleNamespace(
            base=namespace["VispyBaseLayer"],
            namespace=namespace,
            trace=trace,
            state=state,
            model_type=model_type,
            emitter_group=EmitterGroup,
            event=Event,
        )

    return build


def _compare(implementations, scenario):
    expected = scenario(implementations(PINNED))
    observed = scenario(implementations(CURRENT))
    assert observed == expected
    return observed


def _error(function):
    try:
        function()
    except Exception as error:
        return type(error).__name__, re.sub(r"0x[0-9a-fA-F]+", "0xADDRESS", str(error))
    return None


def _array(value):
    array = np.asarray(value)
    return array.shape, str(array.dtype), array.tolist()


def _node(impl):
    class Node:
        def __init__(self):
            self._transform = MatrixTransform()
            self.parent = object()
            self.children = []
            self.order = 3
            self.visible = False
            self.opacity = None
            self.clipping_planes = None
            self.fail_transform = False
            self.fail_gl = False

        @property
        def transform(self):
            return self._transform

        @transform.setter
        def transform(self, value):
            impl.trace.append(("transform", type(value).__name__))
            if self.fail_transform:
                raise RuntimeError("transform publication failed")
            self._transform = value

        def update(self):
            impl.trace.append(("update",))

        def set_gl_state(self, **kwargs):
            impl.trace.append(("gl", kwargs))
            if self.fail_gl:
                raise RuntimeError("GL publication failed")

    return Node()


def _layer(impl, ndim=2, ndisplay=2):
    registry = pint.get_application_registry()
    layer = impl.model_type()
    layer.ndim = ndim
    layer.units = (registry.um,) * ndim
    layer.visible, layer.opacity, layer.blending = True, 0.4, "translucent"
    layer.multiscale = False
    layer.scale = np.arange(2, ndim + 2, dtype=float)
    layer.data_level = 0
    layer.downsample_factors = np.array(
        [np.ones(ndim), np.full(ndim, 2), np.full(ndim, 4)]
    )
    layer.corner_pixels = np.zeros((2, ndim), dtype=int)
    layer._slice_input = SimpleNamespace(
        ndisplay=ndisplay,
        displayed=np.arange(ndisplay, dtype=int),
    )
    layer.events = impl.emitter_group(
        source=layer, **{name: impl.event for name, _ in EVENT_CALLBACKS}
    )
    clips = type("ControlledClippingPlanes", (), {})()
    clips.values = np.arange(12).reshape(2, 2, 3)
    clips.events = impl.emitter_group(source=clips, changed=impl.event)
    clips.as_array = lambda: clips.values
    layer.experimental_clipping_planes = clips

    class SlicedTransform:
        fail = False

        def set_slice(self, displayed):
            impl.trace.append(("slice", tuple(int(x) for x in displayed)))
            if self.fail:
                raise RuntimeError("slice transform failed")
            axes = np.asarray(displayed)
            return SimpleNamespace(
                translate=np.arange(5, 5 + ndim, dtype=float)[axes],
                linear_matrix=np.diag(layer.scale)[np.ix_(axes, axes)],
            )

    transform = SlicedTransform()
    layer._transforms = SimpleNamespace(simplified=transform)
    layer._data_to_world = transform
    return layer


def _new(impl, *, layer=None, node=None):
    class Concrete(impl.base):
        def _on_data_change(self):
            impl.trace.append(("data",))

    layer = _layer(impl) if layer is None else layer
    node = _node(impl) if node is None else node
    controller = Concrete(layer, node, object())
    return controller, layer, node


def _callbacks(controller, layer):
    result = []
    for name, _ in EVENT_CALLBACKS:
        values = getattr(layer.events, name).callbacks
        result.append(
            (name, tuple((ref() is controller, method) for ref, method in values))
        )
    return result


def test_all_twenty_bodies_have_independent_preserved_metadata(implementations):
    first, second = implementations(PINNED), implementations(CURRENT)
    count = 0
    for name, member in vars(first.base).items():
        other = vars(second.base).get(name)
        functions = (
            ((member.fget, other.fget), (member.fset, other.fset))
            if isinstance(member, property)
            else ((member, other),)
        )
        for original, current in functions:
            if not inspect.isfunction(original):
                continue
            assert original is not current
            assert str(inspect.signature(original)) == str(inspect.signature(current))
            assert original.__doc__ == current.__doc__
            assert original.__qualname__ == current.__qualname__
            assert getattr(original, "__isabstractmethod__", False) == getattr(
                current, "__isabstractmethod__", False
            )
            count += 1
    assert count == 20


def test_constructor_publishes_exact_fields_before_real_weak_connections(
    implementations,
):
    def scenario(impl):
        controller, layer, node = _new(impl)
        return (
            tuple(vars(controller)),
            controller.layer is layer,
            controller.node is node,
            controller.events,
            controller._array_like,
            controller.first_visible,
            controller._world_units is layer.units,
            controller._world_to_layer_units_scale,
            (controller.MAX_TEXTURE_SIZE_2D, controller.MAX_TEXTURE_SIZE_3D),
            _callbacks(controller, layer),
            tuple(
                (ref() is controller, name)
                for ref, name in layer.experimental_clipping_planes.events.callbacks
            ),
            impl.trace,
        )

    _compare(implementations, scenario)


def test_constructor_query_and_connection_failures_preserve_partial_state(
    implementations,
):
    def scenario(impl):
        output = []
        for failure in ("query", "unpack", "connect"):
            layer, node = _layer(impl), _node(impl)

            class Concrete(impl.base):
                def _on_data_change(self):
                    pass

            controller = object.__new__(Concrete)
            impl.state.query_error = (
                RuntimeError("capacity query failed") if failure == "query" else None
            )
            impl.state.limits = (8,) if failure == "unpack" else (8, 4)
            if failure == "connect":

                def fail(callback):
                    raise RuntimeError("opacity connection failed")

                layer.events.opacity.connect = fail
            error = _error(
                lambda controller=controller, layer=layer, node=node, cls=Concrete: (
                    cls.__init__(controller, layer, node, object())
                )
            )
            output.append(
                (failure, error, tuple(vars(controller)), _callbacks(controller, layer))
            )
        return output

    _compare(implementations, scenario)


def test_callbacks_remain_weak_and_resolve_replaced_named_methods(implementations):
    def scenario(impl):
        controller, layer, node = _new(impl)
        normalized = layer.events.refresh.callbacks[0]
        before = normalized[0]() is controller, normalized[1]
        controller._on_refresh_change = lambda: impl.trace.append(("replacement",))
        impl.trace.clear()
        layer.events.refresh()
        trace = list(impl.trace)
        reference = weakref.ref(controller)
        del controller
        gc.collect()
        layer.events.refresh()
        return (
            before,
            trace,
            reference() is None,
            len(layer.events.refresh.callbacks),
            node.visible,
        )

    _compare(implementations, scenario)


def test_live_dictionary_mutation_controls_node_visibility_and_blending(
    implementations,
):
    def scenario(impl):
        controller, layer, old = _new(impl)
        namespace = vars(controller)
        node = _node(impl)
        namespace["node"], namespace["first_visible"] = node, True
        layer.blending = "minimum"
        controller._on_visible_change()
        controller._on_blending_change()
        return (
            namespace is controller.__dict__,
            node.visible,
            old.visible,
            impl.trace[1:],
        )

    _compare(implementations, scenario)


@pytest.mark.parametrize("dictionary_subclass", [False, True])
def test_dictionary_replacement_preserves_supplied_map_and_saved_alias(
    dictionary_subclass, implementations
):
    def scenario(impl):
        controller, layer, old = _new(impl)
        saved = controller.__dict__

        class Namespace(dict):
            pass

        supplied = Namespace(saved) if dictionary_subclass else dict(saved)
        node = _node(impl)
        supplied["node"] = node
        controller.__dict__ = supplied
        controller._on_opacity_change()
        return (
            vars(controller) is supplied,
            object.__getattribute__(controller, "__dict__") is supplied,
            saved["node"] is old,
            supplied["node"] is node,
            node.opacity,
            old.opacity,
            tuple(saved),
            tuple(supplied),
        )

    _compare(implementations, scenario)


def test_dictionary_delete_restore_and_invalid_assignment_keep_saved_state(
    implementations,
):
    def scenario(impl):
        controller, layer, node = _new(impl)
        saved = controller.__dict__
        invalid = _error(lambda: object.__setattr__(controller, "__dict__", []))
        retained = controller.__dict__ is saved
        del controller.__dict__
        missing = _error(controller._on_opacity_change)
        empty = vars(controller)
        controller.__dict__ = saved
        controller._on_opacity_change()
        return (
            invalid,
            retained,
            missing,
            empty == {},
            empty is not saved,
            saved["node"] is node,
            node.opacity,
        )

    _compare(implementations, scenario)


def test_descriptors_override_dictionary_keys_and_world_units_publish_before_callbacks(
    implementations,
):
    def scenario(impl):
        controller, layer, node = _new(impl)
        controller.__dict__["world_units"] = "shadow"
        controller.__dict__["order"] = 99
        original = controller.world_units is layer.units, controller.order
        controller._on_matrix_change = lambda: impl.trace.append(
            ("units.matrix", controller._world_to_layer_units_scale)
        )
        registry = pint.get_application_registry()
        controller.world_units = (registry.nm,) * 2
        scaled = controller._world_to_layer_units_scale
        controller.world_units = None
        return (
            original,
            scaled,
            controller._world_units is layer.units,
            controller._world_to_layer_units_scale,
            impl.trace[1:],
        )

    _compare(implementations, scenario)


def test_world_unit_conversion_failure_keeps_new_units_and_previous_scale(
    implementations,
):
    def scenario(impl):
        controller, _, _ = _new(impl)
        previous = controller._world_to_layer_units_scale
        controller._on_matrix_change = lambda: impl.trace.append(("matrix",))
        supplied = (object(), object())
        failure = _error(lambda: setattr(controller, "world_units", supplied))
        return (
            failure,
            controller._world_units == supplied,
            controller._world_to_layer_units_scale is previous,
            impl.trace,
        )

    _compare(implementations, scenario)


def test_default_copy_deepcopy_and_reduction_use_the_canonical_namespace(
    implementations,
):
    def scenario(impl):
        class Concrete(impl.base):
            def _on_data_change(self):
                pass

        controller = object.__new__(Concrete)
        controller.token = [1, 2]
        controller.alias = controller.token
        shallow, deep = copy.copy(controller), copy.deepcopy(controller)
        reduction = controller.__reduce_ex__(4)
        restored = reduction[0](*reduction[1])
        restored.__dict__.update(reduction[2])
        return (
            shallow.__dict__ is not controller.__dict__,
            shallow.token is controller.token,
            deep.token == controller.token,
            deep.token is not controller.token,
            deep.alias is deep.token,
            reduction[0].__name__,
            reduction[1][0] is Concrete,
            reduction[2] is controller.__dict__,
            restored.__dict__ is not controller.__dict__,
            restored.token is controller.token,
        )

    _compare(implementations, scenario)


def test_abc_generic_slotted_subclass_and_cooperative_super_preserve_early_fields(
    implementations,
):
    def scenario(impl):
        class Tail:
            def __init__(self):
                impl.trace.append(("tail.init", self.early))
                super().__init__()

        class Concrete(impl.base, Tail):
            __slots__ = ("early",)

            def __init__(self, layer, node):
                self.early = "before-base"
                self._last_order = None
                super().__init__(layer, node, object())

            def _on_data_change(self):
                impl.trace.append(("data",))

        controller = Concrete(_layer(impl), _node(impl))
        return (
            inspect.isabstract(impl.base),
            impl.base.__abstractmethods__,
            _error(lambda: impl.base(None, None, None)),
            impl.base[impl.model_type].__origin__ is impl.base,
            controller.early,
            controller._last_order,
            tuple(vars(controller)),
            impl.trace,
        )

    _compare(implementations, scenario)


def test_namespace_replacement_publishes_before_old_payload_finalizer(implementations):
    def scenario(impl):
        controller, _, _ = _new(impl)
        reference = weakref.ref(controller)
        observed = []

        class Payload:
            def __del__(self):
                owner = reference()
                observed.append(owner.marker if owner is not None else "dead")

        controller.payload = Payload()
        supplied = dict(vars(controller))
        del supplied["payload"]
        supplied["marker"] = "published"
        controller.__dict__ = supplied
        return observed, controller.__dict__ is supplied

    _compare(implementations, scenario)


def test_controller_namespace_cycle_and_subtype_finalizer_collect_without_fixture_roots(
    implementations,
):
    def scenario(impl):
        finalized = []

        class Concrete(impl.base):
            def _on_data_change(self):
                pass

            def __del__(self):
                finalized.append(self.marker)

        controller = Concrete(_layer(impl), _node(impl), object())
        controller.marker = "cycle"
        controller.loop = controller
        reference = weakref.ref(controller)
        del controller
        gc.collect()
        return reference() is None, finalized

    _compare(implementations, scenario)


def test_master_transform_translation_view_and_scale_copy_preserve_aliases(
    implementations,
):
    def scenario(impl):
        controller, _, node = _new(impl)
        node._transform = object()
        transform = controller._master_transform
        matrix = np.diag([2.0, 3.0, 4.0, 1.0])
        matrix[-1, :3] = [5, 6, 7]
        transform.matrix = matrix
        translation, scale = controller.translate, controller.scale
        translation[0] = 11
        scale[0] = 100
        return (
            controller._master_transform is transform,
            np.shares_memory(translation, transform.matrix),
            np.shares_memory(scale, transform.matrix),
            _array(transform.matrix),
            impl.trace[1:],
        )

    _compare(implementations, scenario)


def test_refresh_visible_opacity_noops_and_abstract_data_have_original_effects(
    implementations,
):
    def scenario(impl):
        controller, layer, node = _new(impl)
        controller._on_refresh_change()
        controller._on_visible_change()
        controller._on_opacity_change()
        direction = controller._on_view_direction_change(np.ones(3), np.zeros(3))
        poll = controller._on_poll(object())
        abstract = _error(lambda: impl.base._on_data_change(controller))
        return node.visible, node.opacity, direction, poll, abstract, impl.trace[1:]

    _compare(implementations, scenario)


@pytest.mark.parametrize(
    "blending,first_visible",
    [("opaque", False), ("minimum", True), ("additive", True), ("translucent", True)],
)
def test_blending_copies_source_state_and_preserves_first_visible_policies(
    blending, first_visible, implementations
):
    def scenario(impl):
        controller, layer, _ = _new(impl)
        layer.blending, controller.first_visible = blending, first_visible
        original = copy.deepcopy(impl.namespace["BLENDING_MODES"])
        controller._on_blending_change()
        return original == impl.namespace["BLENDING_MODES"], impl.trace[1:]

    _compare(implementations, scenario)


def test_blending_errors_and_order_callback_failure_keep_partial_publication(
    implementations,
):
    def scenario(impl):
        controller, layer, node = _new(impl)
        layer.blending = "missing"
        unknown = _error(controller._on_blending_change)
        layer.blending = "opaque"
        node.fail_gl = True
        upload = _error(controller._on_blending_change)

        def fail():
            raise RuntimeError("blending callback failed")

        controller._on_blending_change = fail
        order = _error(lambda: setattr(controller, "order", 17))
        return unknown, upload, order, node.order, impl.trace[1:]

    _compare(implementations, scenario)


def test_reset_dispatch_order_and_failure_stop_at_the_original_callback(
    implementations,
):
    def scenario(impl):
        controller, _, _ = _new(impl)
        names = (
            "_on_visible_change",
            "_on_opacity_change",
            "_on_blending_change",
            "_on_matrix_change",
            "_on_experimental_clipping_planes_change",
            "_on_view_direction_change",
        )
        for name in names:
            setattr(controller, name, lambda name=name: impl.trace.append((name,)))
        controller.reset()
        complete = impl.trace[1:]
        impl.trace.clear()

        def fail():
            impl.trace.append(("matrix.failure",))
            raise RuntimeError("matrix callback failed")

        controller._on_matrix_change = fail
        failure = _error(controller.reset)
        return complete, failure, impl.trace

    _compare(implementations, scenario)


@pytest.mark.parametrize("available", ["both", "no_node", "no_layer"])
def test_clipping_checks_both_boundaries_and_publishes_an_axis_reversal_alias(
    available, implementations
):
    def scenario(impl):
        controller, layer, node = _new(impl)
        values = layer.experimental_clipping_planes.values
        if available == "no_node":
            del node.clipping_planes
        if available == "no_layer":
            del layer.experimental_clipping_planes
        controller._on_experimental_clipping_planes_change()
        result = getattr(node, "clipping_planes", None)
        return (
            result is None,
            None if result is None else _array(result),
            result is not None and np.shares_memory(result, values),
        )

    _compare(implementations, scenario)


@pytest.mark.parametrize(
    "policy",
    ["ordinary2", "array2", "pyramid2", "pyramid3_fine", "pyramid3_tile", "ordinary3"],
)
def test_matrix_axis_order_units_pixel_center_rendered_level_and_child_offsets(
    policy, implementations
):
    def scenario(impl):
        dimension = 3 if "3" in policy else 2
        layer = _layer(impl, ndim=dimension, ndisplay=dimension)
        controller, _, node = _new(impl, layer=layer)
        controller._array_like = policy.startswith(("array", "pyramid"))
        layer.multiscale = policy.startswith("pyramid")
        if policy in ("pyramid2", "pyramid3_tile"):
            layer.corner_pixels[0] = np.arange(1, dimension + 1)
            layer.data_level = 2
        controller._world_to_layer_units_scale = tuple(range(2, dimension + 2))
        child = SimpleNamespace(transform=MatrixTransform())
        node.children = [child]
        controller._on_matrix_change()
        return (
            _array(node.transform.matrix),
            _array(child.transform.matrix),
            impl.trace[1:],
        )

    _compare(implementations, scenario)


def test_matrix_dimension_repair_precedes_slice_failure_and_preserves_old_matrix(
    implementations,
):
    def scenario(impl):
        layer = _layer(impl, ndim=3, ndisplay=3)
        controller, _, node = _new(impl, layer=layer)
        controller._world_to_layer_units_scale = (9,)
        controller._world_units = (pint.get_application_registry().nm,)
        layer._transforms.simplified.fail = True
        previous = node.transform.matrix.copy()
        failure = _error(controller._on_matrix_change)
        return (
            failure,
            controller._world_units is layer.units,
            controller._world_to_layer_units_scale,
            np.array_equal(previous, node.transform.matrix),
            impl.trace[1:],
        )

    _compare(implementations, scenario)


def test_matrix_child_failure_occurs_after_parent_and_previous_child_publication(
    implementations,
):
    def scenario(impl):
        controller, _, node = _new(impl)
        controller._array_like = True
        first = SimpleNamespace(transform=MatrixTransform())

        class FailingTransform:
            @property
            def matrix(self):
                return None

            @matrix.setter
            def matrix(self, value):
                impl.trace.append(("child.failure", _array(value)))
                raise RuntimeError("child transform failed")

        node.children = [first, SimpleNamespace(transform=FailingTransform())]
        failure = _error(controller._on_matrix_change)
        return (
            failure,
            _array(node.transform.matrix),
            _array(first.transform.matrix),
            impl.trace[1:],
        )

    _compare(implementations, scenario)


def test_close_disconnects_only_layer_group_and_keeps_namespace_and_partial_errors(
    implementations,
):
    def scenario(impl):
        output = []
        for fail in (False, True):
            controller, layer, node = _new(impl)
            namespace = controller.__dict__
            transform, parent = node.transform, node.parent
            node.fail_transform = fail
            error = _error(controller.close)
            output.append(
                (
                    error,
                    controller.__dict__ is namespace,
                    controller.layer is layer,
                    controller.node is node,
                    node.transform is transform,
                    node.parent is parent,
                    tuple(
                        len(getattr(layer.events, name).callbacks)
                        for name, _ in EVENT_CALLBACKS
                    ),
                    tuple(
                        (ref() is controller, name)
                        for ref, name in (
                            layer.experimental_clipping_planes.events.callbacks
                        )
                    ),
                )
            )
            if not fail:
                controller.close()
                assert controller.__dict__ is namespace and node.parent is None
        return output

    _compare(implementations, scenario)
