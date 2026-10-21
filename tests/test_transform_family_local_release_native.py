"""Two paired error-path leases for rotation/shear destination cleanup.

This supplements, and does not modify, the frozen 14-case protocol artifact.
The oracle executes guarded complete pinned utility source; the current side
uses actual installed production functions. Fresh destinations have no saved
strong aliases. The backend store callback drops its own argument references
before raising, so its traceback does not mask the application-local leases.
Holding and releasing the exception observes full -> matrix -> rank cleanup;
private frame/code identity and wider provider contracts are not asserted.
"""

from __future__ import annotations

import ast
import hashlib
import weakref
from pathlib import Path
from types import ModuleType, SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari/utils/transforms/transform_utils.py"
PINNED_SHA = "08b5d48b62796795a01d3efddf95ec2f1e36fd08b92f213053c559644bbe785c"


@pytest.fixture(scope="module")
def utilities():
    from napari.utils.transforms import transform_utils as current
    from naparixx import _native

    assert callable(_native.policy_rotate_to_matrix)
    assert callable(_native.policy_shear_to_matrix)
    raw = PINNED.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PINNED_SHA
    tree = ast.parse(raw.decode("utf-8"), filename=str(PINNED))
    original = ModuleType("_protocol_local_release_pinned_utilities")
    exec(compile(tree, str(PINNED), "exec"), vars(original))
    return original, current


@pytest.mark.parametrize("component", ["rotate", "shear"])
def test_error_traceback_retains_then_releases_destination_matrix_rank(
    utilities, monkeypatch, component
):
    def observe(module):
        trace = []
        references = {}

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
                value = Rank()
                references["rank"] = weakref.ref(value)
                return (value,)

            def __del__(self):
                trace.append("matrix.release")

        class FullMatrix:
            def __setitem__(self, key, value):
                assert key == (slice(-2, None), slice(-2, None))
                assert isinstance(value, Matrix)
                trace.append("store")
                # The provider's error frame retains no instance arguments.
                # Application locals, rather than these argument aliases,
                # must keep the destination and component alive afterward.
                del self, key, value
                raise RuntimeError("backend store")

            def __del__(self):
                trace.append("full.release")

        def eye(ndim):
            assert ndim == 3
            trace.append("eye")
            value = FullMatrix()
            references["full"] = weakref.ref(value)
            return value

        def matrix(value):
            trace.append("matrix")
            result = Matrix()
            references["matrix"] = weakref.ref(result)
            return result

        with monkeypatch.context() as patch:
            patch.setattr(module, "np", SimpleNamespace(eye=eye))
            patch.setattr(module, f"_make_{component}_mat", matrix)
            saved_error = None
            try:
                getattr(module, f"{component}_to_matrix")(object(), ndim=3)
            except RuntimeError as error:
                assert str(error) == "backend store"
                saved_error = error
            else:
                pytest.fail("the destination store must fail")

            assert saved_error is not None
            assert saved_error.__traceback__ is not None
            assert set(references) == {"full", "matrix", "rank"}
            assert all(reference() is not None for reference in references.values())
            assert trace == ["eye", "matrix", "shape", "neg", "neg", "store"]

            saved_error.__traceback__ = None
            assert all(reference() is None for reference in references.values())
            assert trace == [
                "eye",
                "matrix",
                "shape",
                "neg",
                "neg",
                "store",
                "full.release",
                "matrix.release",
                "rank.release",
            ]
        return trace

    expected = observe(utilities[0])
    assert observe(utilities[1]) == expected
