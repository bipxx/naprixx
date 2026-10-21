"""Transform application decisions compared with independent pinned sources."""

import gc
import importlib.util
import inspect
import sys
import weakref
from pathlib import Path

import numpy as np
import pytest

from napari.utils.transforms import transforms as actual


@pytest.fixture(scope="module")
def reference():
    root = (
        Path(__file__).resolve().parents[1]
        / ".upstream/napari/src/napari/utils/transforms"
    )
    loaded = []

    def load(filename):
        name = "_original_transform_objects_" + filename[:-3]
        spec = importlib.util.spec_from_file_location(name, root / filename)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        loaded.append(name)
        spec.loader.exec_module(module)
        return module

    # Give the reference independent numerical/unit helper implementations too.
    # EventedList and external NumPy/Pint/psygnal remain shared dependencies.
    utilities = load("transform_utils.py")
    units = load("_units.py")
    replacements = {
        "napari.utils.transforms.transform_utils": utilities,
        "napari.utils.transforms._units": units,
    }
    previous = {name: sys.modules[name] for name in replacements}
    try:
        sys.modules.update(replacements)
        module = load("transforms.py")
    finally:
        sys.modules.update(previous)
    yield module
    for name in loaded:
        sys.modules.pop(name, None)


def equal(actual_value, expected):
    if isinstance(expected, np.ndarray):
        assert isinstance(actual_value, np.ndarray)
        assert actual_value.dtype == expected.dtype
        np.testing.assert_allclose(actual_value, expected, rtol=1e-10, atol=1e-10)
    elif isinstance(expected, dict):
        assert list(actual_value) == list(expected)
        for key in expected:
            equal(actual_value[key], expected[key])
    elif isinstance(expected, (list, tuple)):
        assert type(actual_value) is type(expected)
        assert len(actual_value) == len(expected)
        for value, original in zip(actual_value, expected, strict=True):
            equal(value, original)
    else:
        assert actual_value == expected


def error(function):
    try:
        function()
    except Exception as exception:
        return type(exception).__name__, str(exception)
    return None


def snapshot(transform):
    result = {"type": type(transform).__name__, "name": transform.name}
    for field in ("ndim", "scale", "translate", "rotate", "shear", "linear_matrix"):
        try:
            value = getattr(transform, field)
        except AttributeError:
            continue
        except Exception as exception:
            result[field] = (type(exception).__name__, str(exception))
        else:
            result[field] = value.copy() if isinstance(value, np.ndarray) else value
    if hasattr(transform, "axis_labels"):
        result["axis_labels"] = transform.axis_labels
        result["units"] = tuple(map(str, transform.units))
        result["affine_matrix"] = transform.affine_matrix.copy()
    try:
        result["diagonal"] = transform._is_diagonal
    except Exception as exception:
        result["diagonal"] = (type(exception).__name__, str(exception))
    return result


def compare(reference, scenario):
    expected = scenario(reference)
    result = scenario(actual)
    equal(result, expected)
    return result


@pytest.mark.parametrize("kind", ["ScaleTranslate", "Affine", "CompositeAffine"])
@pytest.mark.parametrize(
    "scale,translate",
    [((1, 2), (3, 4)), ((2,), (1, 2, 3)), ((2, 3, 4), (1,)), ((), ())],
)
def test_constructor_dimensions(reference, kind, scale, translate):
    compare(
        reference,
        lambda module: snapshot(
            getattr(module, kind)(scale=scale, translate=translate, name="example")
        ),
    )


@pytest.mark.parametrize("kind", ["Affine", "CompositeAffine"])
@pytest.mark.parametrize(
    "options",
    [
        {"rotate": 23},
        {"rotate": (12, 30, 45), "scale": (2, 3, 4)},
        {"shear": (0.2,)},
        {"shear": np.array([[1.0, 0], [0.3, 1]])},
        {"ndim": 4, "scale": (2, 3)},
        {"axis_labels": ("y", "x"), "units": ("mm", "um")},
    ],
)
def test_affine_constructor_metadata_and_geometry(reference, kind, options):
    compare(reference, lambda module: snapshot(getattr(module, kind)(**options)))


@pytest.mark.parametrize(
    "options",
    [
        {"linear_matrix": np.array([[2, 0.1], [0.3, 4]])},
        {"affine_matrix": np.array([[2.0, 0.1, 4], [0.3, 4, 5], [0, 0, 1]])},
        {"linear_matrix": np.diag((2.0, 3.0, 4.0)), "ndim": 2},
    ],
)
def test_explicit_affine_matrices(reference, options):
    compare(reference, lambda module: snapshot(module.Affine(**options)))


@pytest.mark.parametrize("kind", ["Affine", "CompositeAffine"])
@pytest.mark.parametrize("rotation", [None, 27])
@pytest.mark.parametrize(
    "field,value",
    [
        ("scale", (3, 5)),
        ("translate", (-2, 7)),
        ("rotate", 36),
        ("shear", (0.4,)),
        ("shear", np.array([[1.0, 0], [0.2, 1]])),
        ("axis_labels", ("a", "b")),
        ("units", "mm"),
    ],
)
def test_property_transitions_events_and_cache(reference, kind, rotation, field, value):
    def scenario(module):
        transform = getattr(module, kind)(scale=(2, 3), rotate=rotation)
        previous = transform.inverse
        changes = []

        def changed():
            changes.append(transform.translate.copy())

        transform.changed.connect(changed)
        failure = error(lambda: setattr(transform, field, value))
        return (
            failure,
            changes,
            previous is transform.inverse,
            transform.inverse is transform.inverse,
            snapshot(transform),
        )

    compare(reference, scenario)


@pytest.mark.parametrize("kind", ["ScaleTranslate", "Affine", "CompositeAffine"])
@pytest.mark.parametrize("axes", [[0, 2], [2, 0], [-1, 0], []])
def test_slice_and_metadata(reference, kind, axes):
    def scenario(module):
        transform = getattr(module, kind)(
            scale=(2, 3, 4), translate=(5, 6, 7), name="slice"
        )
        if kind != "ScaleTranslate":
            transform.axis_labels = ("z", "y", "x")
            transform.units = ("mm", "um", "nm")
        return snapshot(transform.set_slice(axes))

    compare(reference, scenario)


@pytest.mark.parametrize("kind", ["ScaleTranslate", "Affine", "CompositeAffine"])
@pytest.mark.parametrize("axes", [[], [0], [1], [0, 3]])
def test_expand_dimensions(reference, kind, axes):
    compare(
        reference,
        lambda module: snapshot(
            getattr(module, kind)(
                scale=(2, 3), translate=(4, 5), name="expanded"
            ).expand_dims(axes)
        ),
    )


@pytest.mark.parametrize("kind", ["ScaleTranslate", "Affine", "CompositeAffine"])
def test_inverse_and_composition(reference, kind):
    def scenario(module):
        first = getattr(module, kind)(scale=(2, 3), translate=(-3, 7))
        second = getattr(module, kind)(scale=(4, 5), translate=(9, 2))
        coordinates = np.array([[3.0, 5.0], [-4.0, 2.0]])
        return (
            first.inverse(first(coordinates)),
            first.compose(second)(coordinates),
            first.inverse is first.inverse,
        )

    compare(reference, scenario)


@pytest.mark.parametrize("kind", ["ScaleTranslate", "Affine", "CompositeAffine"])
@pytest.mark.parametrize("mutation", ["set", "delete", "property", "append"])
def test_chain_mutation_cache_and_events(reference, kind, mutation):
    def scenario(module):
        factory = getattr(module, kind)
        first, second = factory(scale=(2, 3)), factory(scale=(4, 5))
        chain = module.TransformChain([first, second])
        old_inverse = chain.inverse
        old_simplified = chain.simplified
        changed = []

        def on_changed():
            changed.append(tuple(chain((1.0, 2.0))))

        chain.changed.connect(on_changed)
        if mutation == "set":
            chain[0] = factory(scale=(-2, 6))
        elif mutation == "delete":
            del chain[1]
        elif mutation == "property":
            first.scale = (-2, 6)
        else:
            chain.append(factory(scale=(-2, 6)))
        return (
            chain((1.0, 2.0)),
            chain.inverse((1.0, 2.0)),
            chain.simplified((1.0, 2.0)),
            old_inverse is chain.inverse,
            old_simplified is chain.simplified,
            chain[:1] is chain[:1],
            changed,
        )

    compare(reference, scenario)


def test_arbitrary_transform_callbacks_and_chain_snapshot(reference):
    def scenario(module):
        output = []
        chain = module.TransformChain()

        def first(value):
            output.append("first")
            chain.append(module.Transform(lambda x: x + 100))
            return value + 1

        def second(value):
            output.append("second")
            return value * 3

        chain.extend([module.Transform(first), module.Transform(second)])
        result = chain(4)
        identity = module.Transform()
        return (
            result,
            output,
            len(chain),
            identity.inverse(7),
            identity.inverse is identity.inverse,
            error(lambda: module.Transform(first).inverse),
            error(lambda: identity.set_slice([0])),
            error(lambda: identity.expand_dims([0])),
            error(lambda: module.TransformChain().simplified),
        )

    compare(reference, scenario)


@pytest.mark.parametrize("kind", ["Affine", "CompositeAffine"])
@pytest.mark.parametrize(
    "field,value",
    [
        ("axis_labels", ("one",)),
        ("units", ("mm",)),
        ("shear", [[1, 2], [3, 1]]),
        ("linear_matrix", np.eye(2)),
        ("affine_matrix", np.eye(3)),
    ],
)
def test_setter_failures_and_partial_state(reference, kind, field, value):
    def scenario(module):
        transform = getattr(module, kind)(scale=(2, 3))
        result = error(lambda: setattr(transform, field, value))
        return result, snapshot(transform)

    compare(reference, scenario)


def test_replace_slice_and_array_protocol(reference):
    def scenario(module):
        transform = module.Affine(scale=(2, 3, 4), translate=(5, 6, 7))
        replacement = module.Affine(scale=(9, 10), translate=(11, 12))
        changed = transform.replace_slice([0, 2], replacement)
        return (
            snapshot(changed),
            np.asarray(transform),
            error(lambda: transform.replace_slice([0], replacement)),
        )

    compare(reference, scenario)


def test_signatures_docstrings_and_public_hooks(reference):
    for kind in (
        "Transform",
        "TransformChain",
        "ScaleTranslate",
        "Affine",
        "CompositeAffine",
    ):
        expected, current = getattr(reference, kind), getattr(actual, kind)
        assert current.__doc__ == expected.__doc__
        # Class annotations refer to their own independent module identities.
        for name, method in vars(expected).items():
            if isinstance(method, property):
                assert getattr(current, name).__doc__ == method.__doc__
            elif inspect.isfunction(method):
                current_method = getattr(current, name)
                assert current_method.__doc__ == method.__doc__
                a = inspect.signature(current_method)
                b = inspect.signature(method)
                assert tuple(a.parameters) == tuple(b.parameters)
                for key in a.parameters:
                    assert a.parameters[key].kind == b.parameters[key].kind
                    assert a.parameters[key].default == b.parameters[key].default

    def scenario(module):
        calls = []

        class Child(module.Affine):
            def _clean_cache(self):
                calls.append("clean")
                super()._clean_cache()

            def _setup_decompose_linear_matrix_cache(self):
                calls.append("decompose")
                super()._setup_decompose_linear_matrix_cache()

        child = Child(rotate=12)
        child.scale = (2, 3)
        child.translate = (4, 5)
        return calls, snapshot(child)

    compare(reference, scenario)


@pytest.mark.parametrize("kind", ["Transform", "Affine", "TransformChain"])
def test_callback_cycles_and_cache_lifetime(reference, kind):
    def scenario(module):
        transform = getattr(module, kind)()
        owner = weakref.ref(transform)
        transform.changed.connect(lambda transform=transform: transform.name)
        if kind != "TransformChain":
            inverse = transform.inverse
            inverse_ref = weakref.ref(inverse)
            del inverse
        else:

            def inverse_ref():
                return None

        del transform
        gc.collect()
        return owner() is None, inverse_ref() is None

    compare(reference, scenario)


def test_diagonal_flags_and_subclass_overrides(reference):
    def scenario(module):
        class Custom(module.ScaleTranslate):
            @property
            def _is_diagonal(self):
                return False

        base = module.Transform()
        scale = module.ScaleTranslate()
        custom = Custom()
        return (
            base._is_diagonal is False,
            scale._is_diagonal is True,
            custom._is_diagonal is False,
            module.TransformChain([custom])._is_diagonal is False,
        )

    compare(reference, scenario)


@pytest.mark.parametrize(
    "kind,operation",
    [
        (kind, operation)
        for kind in ("Affine", "CompositeAffine", "ScaleTranslate")
        for operation in (
            "inverse",
            "compose",
            "slice",
            "expand",
            "scale",
            "rotate",
            "shear",
        )
        if kind != "ScaleTranslate" or operation not in ("rotate", "shear")
    ],
)
def test_attribute_provider_capture_order(reference, kind, operation):
    def scenario(module):
        calls = []
        tracked = {
            "ndim",
            "name",
            "scale",
            "translate",
            "rotate",
            "shear",
            "units",
            "axis_labels",
            "affine_matrix",
            "linear_matrix",
            "_linear_matrix",
            "_translate",
            "_scale",
            "_rotate",
            "_shear",
            "_shear_cache",
            "_cache_dict",
            "_is_diagonal",
            "_upper_triangular",
        }

        class Provider(getattr(module, kind)):
            def __getattribute__(self, name):
                if name in tracked:
                    label = object.__getattribute__(self, "__dict__").get(
                        "_label", "first"
                    )
                    calls.append(label + "." + name)
                return super().__getattribute__(name)

        options = {"rotate": 13} if kind != "ScaleTranslate" else {}
        transform = Provider(scale=(2, 3), **options)
        other = Provider(scale=(4, 5), **options)
        other._label = "second"
        calls.clear()
        if operation == "inverse":
            _ = transform.inverse
        elif operation == "compose":
            transform.compose(other)
        elif operation == "slice":
            transform.set_slice([1, 0])
        elif operation == "expand":
            transform.expand_dims([1])
        else:
            setattr(
                transform,
                operation,
                {"scale": (5, 6), "rotate": 21, "shear": (0.3,)}[operation],
            )
        return calls

    compare(reference, scenario)
