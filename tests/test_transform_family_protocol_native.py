"""Small paired regressions for demonstrated Transform/backend protocol gaps.

Run this file alongside the already frozen family/numerical/upstream cohorts.
The current side always imports installed production modules. Independent
oracles compile guarded pinned utility/Transform bodies and the immutable
pre-port naparixx adapter/backend snapshots solely inside this test fixture.
There is no baseline recapture, module-path substitution, production fallback,
or assertion of private frame/code identity.

Five groups have 14 prospective parameter cases: live adapter original cells,
starred-call errors, rotation/shear local release, exception-class results,
and formatted temporary release. External C keyword-dictionary mutation and
generator re-entry are not simulated by Python stand-ins in this cohort.
"""

from __future__ import annotations

import ast
import hashlib
from pathlib import Path
from types import ModuleType, SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari/utils/transforms"
BEFORE = ROOT / ".artifacts/transform-family-owner-before/files/src/naparixx"
SOURCE_HASHES = {
    PINNED / "transforms.py": (
        "3c08fe6aa70cccdf25d420301a99c21015a4b3cd297f22025141f6afa8f8e6ec"
    ),
    PINNED / "transform_utils.py": (
        "08b5d48b62796795a01d3efddf95ec2f1e36fd08b92f213053c559644bbe785c"
    ),
    BEFORE / "_adapters.py": (
        "ee260b56910010d347daec9f274c99520111e3c726dfa87f3c4b381b04f35e95"
    ),
    BEFORE / "_backend.py": (
        "640e6489f5b5b9b611b7f85897a413bd7cc5d6c00199c038a6476fc8f3e12ee1"
    ),
}


def _source(path):
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SOURCE_HASHES[path]
    return ast.parse(raw.decode("utf-8"), filename=str(path))


def _oracle(name, tree, namespace=None):
    module = ModuleType(name)
    if namespace:
        vars(module).update(namespace)
    ast.fix_missing_locations(tree)
    exec(compile(tree, f"<test-only-{name}>", "exec"), vars(module))
    return module


def _extension_imports(tree, namespace):
    """Supply real external extension services to immutable oracle bodies."""
    kept = []
    for node in tree.body:
        if isinstance(node, ast.ImportFrom) and node.level == 1:
            assert node.module is None
            assert all(alias.name in namespace for alias in node.names)
        else:
            kept.append(node)
    tree.body = kept
    return tree


@pytest.fixture(scope="module")
def implementations():
    import toolz
    from psygnal import Signal

    from napari.utils.transforms import transform_utils, transforms
    from naparixx import _adapters, _backend, _native

    # A missing installation fails setup; captured source never substitutes
    # for the current application modules or their native implementations.
    assert issubclass(transforms.Transform, _native.TransformNamespaceBase)
    assert callable(_native.backend_dispatch)
    assert callable(_native.policy_make_rotate_mat)

    utilities = _oracle(
        "_protocol_pinned_utilities", _source(PINNED / "transform_utils.py")
    )
    transform_tree = _source(PINNED / "transforms.py")
    transform_class = next(
        node
        for node in transform_tree.body
        if isinstance(node, ast.ClassDef) and node.name == "Transform"
    )
    # The complete pinned root class is independent. Its tested inverse error
    # branch needs only its original Signal/toolz primitive dependencies.
    transform_tree.body = [
        ast.ImportFrom(
            module="__future__",
            names=[ast.alias(name="annotations")],
            level=0,
        ),
        transform_class,
    ]
    original_transforms = _oracle(
        "_protocol_pinned_transforms",
        transform_tree,
        {"Signal": Signal, "tz": toolz},
    )
    original_adapters = _oracle(
        "_protocol_before_adapters",
        _extension_imports(_source(BEFORE / "_adapters.py"), {"_native": _native}),
        {"_native": _native},
    )
    dependencies = {"_native": _native, "_adapters": original_adapters}
    original_backend = _oracle(
        "_protocol_before_backend",
        _extension_imports(_source(BEFORE / "_backend.py"), dependencies),
        dependencies,
    )
    return (
        SimpleNamespace(
            utilities=utilities,
            transforms=original_transforms,
            adapters=original_adapters,
            backend=original_backend,
        ),
        SimpleNamespace(
            utilities=transform_utils,
            transforms=transforms,
            adapters=_adapters,
            backend=_backend,
        ),
    )


def _failure(call):
    try:
        call()
    except Exception as error:
        return type(error), str(error)
    pytest.fail("the source operation must raise")


@pytest.mark.parametrize("factory_name", ["affine_adapter", "scale_translate_adapter"])
def test_adapter_reads_live_original_cell_after_array_callback(
    implementations, factory_name
):
    def observe(side):
        trace = []
        result = object()
        receiver = SimpleNamespace(
            _linear_matrix=np.eye(2),
            _translate=np.zeros(2),
            scale=np.ones(2),
            translate=np.zeros(2),
        )

        def old_original(self, coords):
            pytest.fail("the array callback replaced this original cell")

        def replacement(self, coords):
            assert self is receiver
            assert coords is provider
            trace.append("replacement")
            return result

        adapted = getattr(side.adapters, factory_name)(old_original)
        cells = dict(
            zip(adapted.__code__.co_freevars, adapted.__closure__, strict=True)
        )
        original_cell = cells["original"]

        class ArrayProvider:
            def __array__(self, dtype=None, copy=None):
                trace.append("array")
                original_cell.cell_contents = replacement
                # Complex coordinates require the source's generic branch.
                return np.array([[1j, 2j]], dtype=dtype)

        provider = ArrayProvider()
        assert adapted(receiver, provider) is result
        assert original_cell.cell_contents is replacement
        return trace

    expected = observe(implementations[0])
    assert expected == ["array", "replacement"]
    assert observe(implementations[1]) == expected


@pytest.mark.parametrize("iter_error", [None, TypeError, LookupError])
def test_starred_rotation_distinguishes_noniterable_and_iterator_errors(
    implementations, monkeypatch, iter_error
):
    def angles(*values):
        pytest.fail("argument expansion must fail before invocation")

    def observe(side):
        trace = []

        class LenOnly:
            def __len__(self):
                trace.append("len")
                return 3

        class IteratorFailure(LenOnly):
            def __iter__(self):
                trace.append("iter")
                raise iter_error("iterator diagnostic")

        value = LenOnly() if iter_error is None else IteratorFailure()

        def array(item):
            assert item is value
            trace.append("array")
            return SimpleNamespace(ndim=1)

        with monkeypatch.context() as patch:
            patch.setattr(
                side.utilities,
                "np",
                SimpleNamespace(isscalar=lambda x: False, array=array),
            )
            patch.setattr(side.utilities, "_make_3d_rotation", angles)
            failure = _failure(lambda: side.utilities._make_rotate_mat(value))
        return trace, failure

    expected = observe(implementations[0])
    if iter_error is None:
        assert expected[0] == ["array", "len"]
        assert expected[1][0] is TypeError
        assert "argument after * must be an iterable, not LenOnly" in expected[1][1]
    else:
        assert expected == (
            ["array", "len", "iter"],
            (iter_error, "iterator diagnostic"),
        )
    assert observe(implementations[1]) == expected


@pytest.mark.parametrize("component", ["rotate", "shear"])
def test_component_local_release_precedes_caller_and_preserves_source_order(
    implementations, monkeypatch, component
):
    def observe(side):
        trace = []

        class Rank:
            def __neg__(self):
                trace.append("neg")
                return -2

            def __del__(self):
                trace.append("rank.release")

        class Matrix:
            @property
            def shape(self):
                trace.append("shape")
                return (Rank(),)

            def __del__(self):
                trace.append("matrix.release")

        class FullMatrix:
            def __setitem__(self, key, value):
                assert key == (slice(-2, None), slice(-2, None))
                assert isinstance(value, Matrix)
                trace.append("store")

        result = FullMatrix()

        def eye(ndim):
            assert ndim == 3
            trace.append("eye")
            return result

        def matrix(value):
            trace.append("matrix")
            return Matrix()

        with monkeypatch.context() as patch:
            patch.setattr(side.utilities, "np", SimpleNamespace(eye=eye))
            patch.setattr(side.utilities, f"_make_{component}_mat", matrix)
            actual = getattr(side.utilities, f"{component}_to_matrix")(object(), ndim=3)
            assert actual is result
            trace.append("caller")
        return trace

    expected = observe(implementations[0])
    assert expected == [
        "eye",
        "matrix",
        "shape",
        "neg",
        "neg",
        "store",
        "matrix.release",
        "rank.release",
        "caller",
    ]
    assert observe(implementations[1]) == expected


@pytest.mark.parametrize(
    "operation", ["transform_inverse", "scalar_shear", "backend_enable"]
)
@pytest.mark.parametrize("invalid_new", [False, True])
def test_exception_factory_returned_class_and_constructor_result(
    implementations, monkeypatch, operation, invalid_new
):
    class BrokenRaised(ValueError):
        def __new__(cls):
            return object()

    exception_class = BrokenRaised if invalid_new else ValueError

    def observe(side):
        trace = []

        def factory(message):
            trace.append(message)
            return exception_class

        with monkeypatch.context() as patch:
            if operation == "transform_inverse":
                patch.setattr(side.transforms, "ValueError", factory, raising=False)
                failure = _failure(
                    lambda: side.transforms.Transform.inverse.fget(
                        SimpleNamespace(_inverse_func=None)
                    )
                )
            elif operation == "scalar_shear":
                patch.setattr(side.utilities, "ValueError", factory, raising=False)
                failure = _failure(lambda: side.utilities._make_shear_mat(1))
            else:
                patch.setattr(side.backend, "RuntimeError", factory, raising=False)
                patch.setattr(
                    side.backend,
                    "importlib",
                    SimpleNamespace(
                        import_module=lambda name: SimpleNamespace(__version__="0.0.0")
                    ),
                )
                failure = _failure(side.backend.enable)
        return trace, failure

    expected = observe(implementations[0])
    assert len(expected[0]) == 1
    if invalid_new:
        assert expected[1][0] is TypeError
        assert (
            "should have returned an instance of BaseException, not <class 'object'>"
            in expected[1][1]
        )
    else:
        assert expected[1] == (ValueError, "")
    assert observe(implementations[1]) == expected


def test_backend_formatted_middle_temporary_releases_before_attribute(
    implementations, monkeypatch
):
    def observe(side):
        trace = []

        class Middle:
            def __format__(self, spec):
                assert spec == ""
                trace.append("middle.format")
                return "Layer."

            def __del__(self):
                trace.append("middle.release")

        class ClassName:
            def __bool__(self):
                trace.append("class.truth")
                return True

            def __add__(self, suffix):
                assert suffix == "."
                trace.append("class.add")
                return Middle()

        class Attribute:
            def __format__(self, spec):
                assert spec == ""
                trace.append("attribute.format")
                return "method"

        with monkeypatch.context() as patch:
            patch.setattr(
                side.backend, "_SPECS", (("module", ClassName(), Attribute(), None),)
            )
            status = side.backend.backend_status()
        assert status["operations"] == ["module.Layer.method"]
        return trace

    expected = observe(implementations[0])
    assert expected == [
        "class.truth",
        "class.add",
        "middle.format",
        "middle.release",
        "attribute.format",
    ]
    assert observe(implementations[1]) == expected
