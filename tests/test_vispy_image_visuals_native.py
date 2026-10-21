"""Finite independent comparisons for 35 Image/visual bodies and shader assembly.

Every targeted class/function is rebuilt from its own pinned or current source,
including Volume's original module shader assembly. Original namespaces never
contain the native module or mount upstream application code. Real NumPy views,
VisPy Frozen/STTransform, and four real VisPy allocation/generated-node cases
exercise the declared backend boundary. Controlled scene/program/GL/event/model
services expose finite publication and failure traces. Scalar/Base controller,
contrast coercion, enum/model dumping and GL-capacity services are explicitly
scoped owned dependencies, not accepted original-algorithm fallbacks. This suite
does not establish GPU drawing, arbitrary plugin MI/offset/frame reflection,
full provider/model closure, clean shutdown, or platform parity.
"""

import ast
import hashlib
import inspect
import itertools
import re
import weakref
from contextlib import contextmanager
from functools import lru_cache
from pathlib import Path
from types import SimpleNamespace
from typing import Any

import numpy as np
import numpy.typing as npt
import pytest
from vispy.util.frozen import Frozen
from vispy.visuals.transforms.linear import STTransform

ROOT = Path(__file__).resolve().parents[1]
PINNED = ROOT / ".upstream/napari/src/napari"
CURRENT = ROOT / "src/napari"
HASHES = {
    "_vispy/layers/image.py": (
        "ea2e3acf287ae9ff50ba2adcae72655f25a421ba8df5ade11f4b06a6c7385402"
    ),
    "_vispy/layers/tiled_image.py": (
        "d834511052054994b946807cea8e328be7ccb70b6bc5665d5505f0450283e6df"
    ),
    "_vispy/visuals/util.py": (
        "b3a0310c45a6f0c10b7daf3b5437c3b97afd272e0e2451f44aee099e00357d39"
    ),
    "_vispy/visuals/image.py": (
        "6bb09bec5b5602db9b476aff0c1e0643ce081696713089cc9ad3a24282617b96"
    ),
    "_vispy/visuals/labels.py": (
        "fbe5e5edbe74a81c08aa006c7d24a5520f0cfc735edec3b4194b18cdb5f4aecc"
    ),
    "_vispy/visuals/volume.py": (
        "6a4cb67c49af07843bfe9fc8db5024b1a14e2302047d3db165ac9369f2fb7922"
    ),
    "_vispy/utils/gl.py": (
        "76278bb2e622cc723ef8cbfc7302b9cba0869520e68475c126aa302c6641f65e"
    ),
    "utils/colormaps/colormap_utils.py": (
        "0de47b92f4f0a90af004a1918b62b835cb1afb7eae3e1014f63829c2a12fbf98"
    ),
}

# Exact original nodes; root retains original parameterization and genuine skips.
# These are a static handoff, not a claim of collected/runtime case counts.
ORIGINAL_SELECTORS = (
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_image_layer.py::test_no_float32_texture_support",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_image_layer.py::test_base_create",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_image_layer.py::test_3d_slice_of_2d_image_with_order",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_image_layer.py::test_2d_slice_of_3d_image_with_order",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_image_layer.py::test_transforming_child_node_pyramid",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_big_images.py::test_big_2D_image",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_big_images.py::test_big_3D_image",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_big_images.py::test_downsample_value",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_labels_layer.py::test_colormap_rebuilt_when_slice_dtypes_change",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_vispy_labels_layer.py::test_labels_iso_gradient_modes",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_image_rendering.py::test_image_rendering",
    ".upstream/napari/src/napari/_vispy/_tests/"
    "test_image_rendering.py::test_visibility_consistency",
    ".upstream/napari/src/napari/utils/colormaps/_tests/"
    "test_colormaps.py::test_colormap",
)


def _source(root, relative):
    raw = (root / relative).read_bytes()
    if root == PINNED:
        assert hashlib.sha256(raw).hexdigest() == HASHES[relative]
    return raw.decode("utf-8")


def _segment(source, node):
    first = min([node.lineno, *(d.lineno for d in getattr(node, "decorator_list", ()))])
    text = "".join(source.splitlines(keepends=True)[first - 1 : node.end_lineno])
    assert ast.dump(ast.parse(text).body[0]) == ast.dump(node)
    return text


def _select(source, *names):
    pieces = []
    for node in ast.parse(source).body:
        targets = [getattr(node, "name", None)]
        if isinstance(node, ast.Assign):
            targets += [t.id for t in node.targets if isinstance(t, ast.Name)]
        if any(name in targets for name in names):
            pieces.append(_segment(source, node))
    return pieces


def _execute(source, pieces, namespace, filename):
    future = (
        "from __future__ import annotations\n"
        if ("from __future__ import annotations" in source)
        else ""
    )
    exec(compile(future + "\n\n".join(pieces), filename, "exec"), namespace)


def _volume_pieces(source):
    # All napari-owned constants, procedural assembly, and complete Volume class.
    # Backend imports alone are supplied explicitly; no live module is executed.
    return [
        _segment(source, node)
        for node in ast.parse(source).body
        if not isinstance(node, (ast.Import, ast.ImportFrom))
    ]


def _compose_native_heap_base(source, name):
    """Change only one test-class base list; retain every owned original body."""
    original = ast.parse(source)
    owner = next(
        n for n in original.body if isinstance(n, ast.ClassDef) and n.name == name
    )
    native_base = ast.parse("_native.VispyNamespaceBase", mode="eval").body
    native_dump = ast.dump(native_base)
    if any(ast.dump(base) == native_dump for base in owner.bases):
        assert ast.dump(owner.bases[0]) == native_dump
        return source
    lines = source.splitlines(keepends=True)
    prefix = "class " + name + "("
    assert lines[owner.lineno - 1].count(prefix) == 1
    lines[owner.lineno - 1] = lines[owner.lineno - 1].replace(
        prefix, prefix + "_native.VispyNamespaceBase, ", 1
    )
    composed = "".join(lines)
    rebuilt = ast.parse(composed)
    rebuilt_owner = next(
        n for n in rebuilt.body if isinstance(n, ast.ClassDef) and n.name == name
    )
    assert ast.dump(rebuilt_owner.bases.pop(0)) == native_dump
    assert ast.dump(rebuilt) == ast.dump(original)
    return composed


def _value(value):
    if isinstance(value, np.ndarray):
        return (value.shape, str(value.dtype), value.tolist())
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, dict):
        return tuple((k, _value(v)) for k, v in value.items())
    if isinstance(value, (tuple, list)):
        return tuple(_value(v) for v in value)
    if isinstance(value, (str, float, int, bool)) or value is None:
        return value
    return type(value).__name__


def _error(callback):
    try:
        callback()
    except Exception as error:
        return (type(error).__name__, re.sub(r"0x[0-9a-fA-F]+", "0xADDR", str(error)))
    return None


def _controlled(state):
    class Program(dict):
        def __setattr__(self, name, value):
            state.trace.append(("program.attr", name, _value(value)))
            if state.fail == "program." + name:
                raise RuntimeError("program " + name + " failed")
            object.__setattr__(self, name, value)

        def __setitem__(self, name, value):
            state.trace.append(("program.item", name, _value(value)))
            if state.fail == "program." + name:
                raise RuntimeError("program " + name + " failed")
            super().__setitem__(name, value)

    class Backend(Frozen):
        _shaders = {"fragment": "HEADER\nvoid main()\nTAIL", "vertex": "VERTEX"}
        _rendering_methods = {"mip": {"backend": "mip"}}

        def __init__(self, data=None, *args, texture_format=None, **kwargs):
            state.trace.append(
                (
                    "backend.init",
                    type(self).__name__,
                    _value(data),
                    _value(kwargs),
                    texture_format,
                )
            )
            state.constructors += 1
            if state.fail == "construct" and state.constructors == state.fail_at:
                raise RuntimeError("backend constructor failed")
            self.unfreeze()
            object.__setattr__(self, "_data", data)
            object.__setattr__(self, "_backend_format", texture_format)
            object.__setattr__(
                self,
                "_texture",
                SimpleNamespace(is_normalized=False, clim_normalized=(0.2, 0.8)),
            )
            object.__setattr__(self, "size", (4, 3))
            object.__setattr__(self, "parent", kwargs.get("parent"))
            object.__setattr__(self, "events", SimpleNamespace())
            object.__setattr__(self, "shared_program", Program())
            object.__setattr__(self, "_func_templates", {"red_to_luminance": "RED"})
            object.__setattr__(self, "cmap", SimpleNamespace(glsl_map="MAP"))
            object.__setattr__(self, "gamma", 1.0)
            object.__setattr__(self, "clim", (0.0, 1.0))
            object.__setattr__(self, "interpolation", "nearest")
            object.__setattr__(self, "attenuation", 0.0)
            object.__setattr__(self, "threshold", 0.0)
            object.__setattr__(self, "mip_cutoff", None)
            object.__setattr__(self, "minip_cutoff", None)
            object.__setattr__(self, "transform", STTransform())
            self.freeze()

        def __setattr__(self, name, value):
            if name in {
                "parent",
                "transform",
                "clim",
                "gamma",
                "interpolation",
                "threshold",
                "attenuation",
                "mip_cutoff",
                "minip_cutoff",
            }:
                state.trace.append(("node.attr", name, _value(value)))
                if state.fail == name:
                    raise RuntimeError("node " + name + " failed")
            super().__setattr__(name, value)

        def unfreeze(self):
            state.trace.append(("unfreeze", type(self).__name__))
            super().unfreeze()

        def freeze(self):
            state.trace.append(("freeze", type(self).__name__))
            super().freeze()

        def update(self):
            state.trace.append(("update", type(self).__name__))
            if state.fail == "update":
                raise RuntimeError("update failed")

        def set_data(self, data):
            state.trace.append(("child.data", _value(data)))
            state.uploads += 1
            if state.fail == "upload" and state.uploads == state.fail_at:
                raise RuntimeError("child upload failed")
            self._data = data

        def set_gl_state(self, *args, **kwargs):
            state.trace.append(("child.gl", _value(args), _value(kwargs)))
            state.gl_calls += 1
            if state.fail == "child.gl" and state.gl_calls == state.fail_at:
                raise RuntimeError("child GL state failed")

        def _transform_changed(self, event=None):
            state.trace.append(("base.transform_changed",))

    class Compound(Backend):
        def __init__(self, subvisuals):
            super().__init__()
            self.unfreeze()
            # This controlled compound exposes the source's child-forwarding
            # fallback; it owns no individual image attributes/events.
            for name in ("cmap", "clim", "gamma", "interpolation", "events"):
                object.__delattr__(self, name)
            self._subvisuals = list(subvisuals)
            self.freeze()

    class Child(Backend):
        @property
        def texture_format(self):
            return self._backend_format

    def function(code):
        state.trace.append(("function", code))
        if state.fail == "function" and code == "MAP":
            raise RuntimeError("map function failed")
        return SimpleNamespace(code=code)

    def chain(*, funcs):
        state.trace.append(("chain", tuple(f.code for f in funcs)))
        return SimpleNamespace(funcs=funcs)

    def colormap(**kwargs):
        state.trace.append(("colormap", _value(kwargs)))
        if state.fail == "colormap":
            raise RuntimeError("colormap construction failed")
        return SimpleNamespace(kwargs=kwargs)

    return SimpleNamespace(
        image=Backend,
        volume=Backend,
        image_visual=Backend,
        child=Child,
        compound=Compound,
        base_visual=Backend,
        create_visual_node=lambda visual: type("Label", (visual,), {}),
        function=function,
        chain=chain,
        colormap=colormap,
    )


@pytest.fixture(scope="module")
def implementations():
    # Imports execute only in root's later frozen runtime, never during preparation.
    from vispy.color import Colormap
    from vispy.scene.visuals import Compound, Image, Volume, create_visual_node
    from vispy.visuals import BaseVisual, ImageVisual
    from vispy.visuals.shaders import Function, FunctionChain

    from napari.layers.base._base_constants import Blending
    from napari.layers.labels._labels_constants import IsoCategoricalGradientMode
    from napari.utils.colormaps.colormap import Colormap as NapariColormap
    from naparixx import _native

    def build(root, *, real=False, native_layout=False):
        state = SimpleNamespace(
            trace=[],
            fail=None,
            fail_at=1,
            constructors=0,
            uploads=0,
            gl_calls=0,
            extensions="texture_float",
            limits=(4, 3),
            connect_hook=None,
        )
        backend = _controlled(state)
        if real:
            backend = SimpleNamespace(
                image=Image,
                volume=Volume,
                image_visual=ImageVisual,
                child=Image,
                compound=Compound,
                base_visual=BaseVisual,
                create_visual_node=create_visual_node,
                function=Function,
                chain=FunctionChain,
                colormap=Colormap,
            )

        def namespace(name, **values):
            result = {"__name__": "_image_visual_contract." + name, "np": np, **values}
            if root == CURRENT:
                result["_native"] = _native
            return result

        def execute(relative, ns, names):
            source = _source(root, relative)
            if native_layout and root == CURRENT:
                name = {
                    "_vispy/visuals/util.py": "TextureMixin",
                    "_vispy/layers/tiled_image.py": "TiledImageNode",
                }.get(relative)
                if name is not None:
                    source = _compose_native_heap_base(source, name)
            _execute(source, _select(source, *names), ns, str(root / relative))

        @contextmanager
        def context():
            state.trace.append(("context.enter",))
            try:
                yield
            finally:
                state.trace.append(("context.exit",))

        def gl_parameter(key):
            state.trace.append(("gl.extensions", key))
            if state.fail == "gl":
                raise RuntimeError("GL query failed")
            return state.extensions

        gl_ns = namespace(
            "gl",
            lru_cache=lru_cache,
            _opengl_context=context,
            gl=SimpleNamespace(GL_EXTENSIONS=7939, glGetParameter=gl_parameter),
        )
        execute("_vispy/utils/gl.py", gl_ns, ["get_gl_extensions"])
        cmap_ns = namespace(
            "cmap", Colormap=NapariColormap, VispyColormap=Colormap
        )
        execute("utils/colormaps/colormap_utils.py", cmap_ns, ["_napari_cmap_to_vispy"])
        cmap_ns["VispyColormap"] = backend.colormap
        util_ns = namespace("util", Visual=type("Visual", (), {}))
        execute("_vispy/visuals/util.py", util_ns, ["TextureMixin"])
        texture = util_ns["TextureMixin"]
        visual_ns = namespace(
            "visual_image", TextureMixin=texture, BaseImage=backend.image
        )
        execute("_vispy/visuals/image.py", visual_ns, ["Image"])
        label_ns = namespace(
            "visual_labels",
            TextureMixin=texture,
            ImageVisual=backend.image_visual,
            Function=backend.function,
            FunctionChain=FunctionChain,
            create_visual_node=backend.create_visual_node,
        )
        execute(
            "_vispy/visuals/labels.py",
            label_ns,
            ["LabelVisual", "BaseLabel", "LabelNode"],
        )
        label_ns["FunctionChain"] = backend.chain
        volume_ns = namespace(
            "visual_volume",
            TextureMixin=texture,
            BaseVolume=backend.volume,
            IsoCategoricalGradientMode=IsoCategoricalGradientMode,
        )
        volume_source = _source(root, "_vispy/visuals/volume.py")
        _execute(
            volume_source,
            _volume_pieces(volume_source),
            volume_ns,
            str(root / "_vispy/visuals/volume.py"),
        )
        tiled_ns = namespace(
            "tiled",
            Any=Any,
            npt=npt,
            itertools=itertools,
            Compound=backend.compound,
            Image=backend.child,
            BaseVisual=BaseVisual,
            STTransform=STTransform,
        )
        execute(
            "_vispy/layers/tiled_image.py",
            tiled_ns,
            ["PASS_THROUGH_ATTRIBUTES", "TiledImageNode", "make_tiles"],
        )
        tiled_ns["BaseVisual"] = backend.base_visual

        parent = _native.VispyNamespaceBase if root == CURRENT else object

        class ScalarFieldLayerNode(parent):
            pass

        class ScalarController(parent):
            def __init__(
                self,
                layer,
                node=None,
                texture_format="auto",
                layer_node_class=None,
                **kwargs,
            ):
                state.trace.append(
                    (
                        "scalar.init",
                        self._last_order,
                        _value(kwargs),
                        texture_format,
                        layer_node_class.__name__,
                    )
                )
                self.layer, self.node = layer, node

            def _on_display_change(self):
                state.trace.append(("scalar.display",))
                if state.fail == "display":
                    raise RuntimeError("display failed")

            def _on_data_change(self):
                state.trace.append(("scalar.data",))

            def _on_rendering_change(self):
                state.trace.append(("scalar.rendering",))

            def _on_blending_change(self):
                state.trace.append(("scalar.blending",))

            def reset(self):
                state.trace.append(("scalar.reset",))

        def capacities():
            state.trace.append(("capacities",))
            return state.limits

        def coerce(values):
            state.trace.append(("contrast.coerce", tuple(values)))
            if state.fail == "coerce":
                raise RuntimeError("contrast conversion failed")
            return SimpleNamespace(contrast_limits=tuple(values))

        image_ns = namespace(
            "layer_image",
            ScalarFieldLayerNode=ScalarFieldLayerNode,
            VispyScalarFieldBaseLayer=ScalarController,
            _VISPY_FORMAT_TO_DTYPE={
                "r8": np.dtype(np.uint8),
                "r16": np.dtype(np.uint16),
                "r32f": np.dtype(np.float32),
                None: np.dtype(np.float32),
            },
            TiledImageNode=tiled_ns["TiledImageNode"],
            ImageNode=visual_ns["Image"],
            VolumeNode=volume_ns["Volume"],
            Blending=Blending,
            Image=type("ControlledImageModel", (), {}),
            get_gl_extensions=gl_ns["get_gl_extensions"],
            get_max_texture_sizes=capacities,
            _coerce_contrast_limits=coerce,
            _napari_cmap_to_vispy=cmap_ns["_napari_cmap_to_vispy"],
        )
        execute(
            "_vispy/layers/image.py", image_ns, ["ImageLayerNode", "VispyImageLayer"]
        )
        return SimpleNamespace(
            root=root,
            real=real,
            state=state,
            trace=state.trace,
            backend=backend,
            namespaces={
                "util": util_ns,
                "image": visual_ns,
                "labels": label_ns,
                "volume": volume_ns,
                "tiled": tiled_ns,
                "layer": image_ns,
                "gl": gl_ns,
                "cmap": cmap_ns,
            },
            texture=texture,
            image=visual_ns["Image"],
            labels=label_ns["LabelNode"],
            label_visual=label_ns["LabelVisual"],
            volume=volume_ns["Volume"],
            tiled=tiled_ns["TiledImageNode"],
            tiles=tiled_ns["make_tiles"],
            factory=image_ns["ImageLayerNode"],
            controller=image_ns["VispyImageLayer"],
            extensions=gl_ns["get_gl_extensions"],
            cmap=cmap_ns["_napari_cmap_to_vispy"],
            blending=Blending,
            gradient=IsoCategoricalGradientMode,
        )

    return build


def _compare(implementations, scenario, *, real=False):
    original, current = (
        implementations(PINNED, real=real),
        implementations(CURRENT, real=real),
    )
    assert scenario(original) == scenario(current)


def _model(impl):
    class Emitter:
        def __init__(self, name):
            self.name = name

        def connect(self, callback):
            impl.trace.append(("connect", self.name, callback.__name__))
            if impl.state.connect_hook is not None:
                impl.state.connect_hook(self.name, callback)
            if impl.state.fail == "connect." + self.name:
                raise RuntimeError(self.name + " connection failed")

    events = SimpleNamespace(
        **{
            name: Emitter(name)
            for name in (
                "interpolation2d",
                "interpolation3d",
                "contrast_limits",
                "gamma",
                "iso_threshold",
                "attenuation",
                "set_data",
            )
        }
    )
    payload = {
        "name": "map",
        "nan_color": (1, 0, 0, 1),
        "colors": [(0, 0, 0, 1), (1, 1, 1, 1)],
    }
    model = SimpleNamespace(
        events=events,
        _slice_input=SimpleNamespace(ndisplay=2, order=(0, 1)),
        interpolation2d="nearest",
        interpolation3d="linear",
        gamma=0.7,
        iso_threshold=4.0,
        attenuation=0.3,
        contrast_limits=(2.0, 8.0),
        contrast_limits_range=(0.0, 10.0),
        blending=impl.blending.TRANSLUCENT,
        colormap=SimpleNamespace(model_dump=lambda: dict(payload)),
    )
    return model


def _bare_controller(impl, *, volume=False, tiled=False):
    controller = object.__new__(impl.controller)
    controller.layer = _model(impl)
    controller._last_order = None
    controller.node = (
        impl.volume(np.zeros((1, 1, 1), dtype=np.float32), texture_format=None)
        if volume
        else (
            impl.tiled(np.arange(6).reshape(2, 3), 2)
            if tiled
            else impl.image(np.zeros((2, 3)), texture_format=None)
        )
    )
    impl.trace.clear()
    return controller


def test_all_thirty_five_owned_bodies_preserve_independent_metadata(implementations):
    original, current = implementations(PINNED), implementations(CURRENT)
    count = 0
    for name in (
        "texture",
        "image",
        "label_visual",
        "labels",
        "volume",
        "tiled",
        "factory",
        "controller",
    ):
        before, after = getattr(original, name), getattr(current, name)
        for key, value in vars(before).items():
            methods = (
                [value]
                if inspect.isfunction(value)
                else ([value.fget, value.fset] if isinstance(value, property) else [])
            )
            other = vars(after)[key]
            counterparts = (
                [other]
                if inspect.isfunction(other)
                else ([other.fget, other.fset] if isinstance(other, property) else [])
            )
            for old, new in zip(methods, counterparts, strict=True):
                if old is None:
                    assert new is None
                    continue
                count += 1
                assert old is not new
                assert str(inspect.signature(old)) == str(inspect.signature(new))
                assert old.__doc__ == new.__doc__
                assert old.__qualname__ == new.__qualname__
    for name in ("tiles", "extensions", "cmap"):
        before, after = getattr(original, name), getattr(current, name)
        assert before is not after
        assert str(inspect.signature(before)) == str(inspect.signature(after))
        assert before.__doc__ == after.__doc__
        count += 1
    assert count == 35


def test_volume_shader_assembly_copies_backend_maps_and_preserves_snippet_aliases(
    implementations,
):
    def scenario(impl):
        ns = impl.namespaces["volume"]
        assert ns["shaders"] is not impl.backend.volume._shaders
        assert ns["rendering_methods"] is not impl.backend.volume._rendering_methods
        assert ns["Volume"]._shaders is ns["shaders"]
        assert ns["Volume"]._rendering_methods is ns["rendering_methods"]
        assert (
            ns["rendering_methods"]["iso_categorical"] is ns["ISO_CATEGORICAL_SNIPPETS"]
        )
        assert (
            ns["rendering_methods"]["translucent_categorical"]
            is ns["TRANSLUCENT_CATEGORICAL_SNIPPETS"]
        )
        assert impl.backend.volume._shaders["fragment"] == "HEADER\nvoid main()\nTAIL"
        assert ns["shaders"]["fragment"] == ns["FAST_GRADIENT_SHADER"]
        return tuple(
            (name, _value(ns[name]))
            for name in (
                "before",
                "after",
                "FAST_GRADIENT_SHADER",
                "SMOOTH_GRADIENT_SHADER",
                "shaders",
                "rendering_methods",
            )
        )

    _compare(implementations, scenario)


@pytest.mark.parametrize("fragment", ["without main", "a void main() b void main() c"])
def test_shader_split_failure_publishes_only_original_partial_globals(
    implementations, fragment
):
    def scenario(impl):
        original_ns = impl.namespaces["volume"]
        backend = type(
            "BadVolume",
            (),
            {"_shaders": {"fragment": fragment}, "_rendering_methods": {}},
        )
        ns = {
            "__name__": original_ns["__name__"],
            "BaseVolume": backend,
            "TextureMixin": impl.texture,
            "IsoCategoricalGradientMode": impl.gradient,
        }
        if impl.root == CURRENT:
            ns["_native"] = original_ns["_native"]
        source = _source(impl.root, "_vispy/visuals/volume.py")
        error = _error(
            lambda: _execute(source, _volume_pieces(source), ns, "volume_shader_oracle")
        )
        assert error is not None and error[0] == "ValueError"
        assert "shaders" in ns and "before" not in ns and "after" not in ns
        assert "FAST_GRADIENT_SHADER" not in ns and "rendering_methods" not in ns
        return error, ns["shaders"]

    _compare(implementations, scenario)


@pytest.mark.parametrize("failure", [False, True])
def test_gl_extensions_cache_identity_and_context_cleanup(implementations, failure):
    def scenario(impl):
        impl.state.fail = "gl" if failure else None
        impl.trace.clear()
        error = _error(impl.extensions)
        if failure:
            assert impl.extensions.cache_info().currsize == 0
            impl.state.fail = None
        first = impl.extensions()
        second = impl.extensions()
        assert first is second
        assert impl.trace[-1] == ("context.exit",)
        return error, first, tuple(impl.trace), tuple(impl.extensions.cache_info())

    _compare(implementations, scenario)


@pytest.mark.parametrize("failure", ["none", "name", "nan_color", "colormap"])
def test_colormap_conversion_mutates_dump_alias_in_source_order(
    implementations, failure
):
    def scenario(impl):
        colors = np.array([[0, 0, 0, 1], [1, 1, 1, 1]], dtype=np.float32)
        payload = {"name": "sample", "nan_color": (1, 0, 0, 1), "colors": colors}
        if failure in {"name", "nan_color"}:
            del payload[failure]
        impl.state.fail = "colormap" if failure == "colormap" else None
        impl.trace.clear()
        model = SimpleNamespace(model_dump=lambda: payload)
        result = []
        error = _error(lambda: result.append(impl.cmap(model)))
        if result:
            assert result[0].kwargs["colors"] is colors
            assert (
                "bad_color" in payload
                and "name" not in payload
                and "nan_color" not in payload
            )
        return error, _value(payload), tuple(impl.trace)

    _compare(implementations, scenario)


@pytest.mark.parametrize(
    "texture_format,extensions",
    [(None, ""), ("auto", ""), ("auto", "texture_float"), ("r32f", "")],
)
def test_image_node_factory_texture_fallback_and_initial_array_dtypes(
    implementations, texture_format, extensions
):
    def scenario(impl):
        impl.state.extensions = extensions
        impl.trace.clear()
        holder = impl.factory(texture_format=texture_format)
        expected = (
            None if texture_format == "auto" and not extensions else texture_format
        )
        assert holder._image_node.texture_format == expected
        assert holder._tiledimage_node.texture_format == expected
        assert holder._volume_node.texture_format == expected
        assert holder._tiledimage_node.data.dtype == np.float32
        assert holder._volume_node._data.shape == (1, 1, 1)
        assert holder._volume_node._data.dtype == np.float32
        assert (holder._image_node._data is None) == (expected in {None, "auto"})
        return tuple(vars(holder)), tuple(impl.trace)

    _compare(implementations, scenario)


def test_node_factory_failure_keeps_original_partial_node_publication(implementations):
    def scenario(impl):
        holder = object.__new__(impl.factory)
        impl.state.fail, impl.state.fail_at = "construct", 3
        impl.trace.clear()
        error = _error(lambda: impl.factory.__init__(holder, texture_format="auto"))
        assert error is not None
        assert "_image_node" in vars(holder) and "_tiledimage_node" not in vars(holder)
        assert "_volume_node" not in vars(holder)
        return error, tuple(vars(holder)), tuple(impl.trace)

    _compare(implementations, scenario)


def test_node_selection_shapes_custom_alias_and_dtype_validation(implementations):
    def scenario(impl):
        holder = impl.factory(texture_format="r32f")
        output = []
        for display, shape, expected in (
            (2, None, "_image_node"),
            (2, (4, 4), "_image_node"),
            (2, (5, 2), "_tiledimage_node"),
            (2, (2, 5, 3), "_tiledimage_node"),
            (2, (1,), "_image_node"),
            (3, (5, 5), "_volume_node"),
            (7, None, "_volume_node"),
        ):
            selected = holder.get_node(display, np.dtype(np.float32), shape)
            assert selected is getattr(holder, expected)
            output.append((display, shape, expected))
        mismatch = _error(lambda: holder.get_node(2, np.dtype(np.uint8), (1, 1)))
        assert mismatch is not None and mismatch[0] == "ValueError"
        custom = object()
        holder._custom_node = custom
        assert holder.get_node(2, np.dtype(np.uint8), (500, 500)) is custom
        return tuple(output), mismatch

    _compare(implementations, scenario)


def test_image_constructor_preserves_early_order_dynamic_connections_and_dispatch(
    implementations,
):
    def scenario(impl):
        layer = _model(impl)
        node = impl.image(np.zeros((2, 3)), texture_format=None)
        impl.trace.clear()

        def hook(name, callback):
            if name == "interpolation2d":
                controller = callback.__self__

                def replacement():
                    impl.trace.append(("replacement.interpolation",))

                controller._on_interpolation_change = replacement

        impl.state.connect_hook = hook
        controller = impl.controller(
            layer, node=node, texture_format=None, font_info="FONT", extra="EXTRA"
        )
        assert controller._last_order is None
        assert impl.trace[0] == (
            "scalar.init",
            None,
            (("font_info", "FONT"), ("extra", "EXTRA")),
            None,
            "ImageLayerNode",
        )
        assert ("connect", "interpolation3d", "replacement") in impl.trace
        assert impl.trace[-1] == ("scalar.data",)
        assert impl.trace.index(("scalar.display",)) < impl.trace.index(
            ("scalar.reset",)
        )
        return tuple(impl.trace), tuple(vars(controller))

    _compare(implementations, scenario)


def test_constructor_display_failure_stops_before_reset_and_data(implementations):
    def scenario(impl):
        controller = object.__new__(impl.controller)
        layer = _model(impl)
        node = impl.image(None, texture_format=None)
        impl.trace.clear()
        impl.state.fail = "display"
        error = _error(lambda: impl.controller.__init__(controller, layer, node=node))
        assert impl.trace[-1] == ("scalar.display",)
        assert ("scalar.reset",) not in impl.trace and (
            "scalar.data",
        ) not in impl.trace
        return error, tuple(vars(controller)), tuple(impl.trace)

    _compare(implementations, scenario)


def test_axis_order_changes_use_current_node_and_commit_after_tile_callback(
    implementations,
):
    def scenario(impl):
        controller = _bare_controller(impl, tiled=True)
        controller._on_axis_order_change()
        first = controller._last_order
        count = len(impl.trace)
        controller._on_axis_order_change()
        assert len(impl.trace) == count
        controller.layer._slice_input.order = (1, 0)
        impl.state.fail = "transform"
        error = _error(controller._on_axis_order_change)
        assert error is not None and controller._last_order is first
        impl.state.fail = None
        controller.node = impl.image(None, texture_format=None)
        controller._on_axis_order_change()
        assert controller._last_order == (1, 0)
        return error, first, controller._last_order, tuple(impl.trace)

    _compare(implementations, scenario)


def test_interpolation_dimension_branch_and_gamma_publication(implementations):
    def scenario(impl):
        controller = _bare_controller(impl)
        controller._on_interpolation_change()
        assert controller.node.interpolation == "nearest"
        controller.layer._slice_input.ndisplay = 3
        controller._on_interpolation_change()
        controller._on_gamma_change()
        assert (
            controller.node.interpolation == "linear" and controller.node.gamma == 0.7
        )
        return tuple(impl.trace)

    _compare(implementations, scenario)


def test_render_blend_reset_keep_real_super_and_fresh_dynamic_callback_order(
    implementations,
):
    def scenario(impl):
        controller = _bare_controller(impl)
        names = (
            "_on_attenuation_change",
            "_on_iso_threshold_change",
            "_update_mip_minip_cutoff",
            "_on_interpolation_change",
            "_on_colormap_change",
            "_on_contrast_limits_change",
            "_on_gamma_change",
        )
        for name in names:
            setattr(controller, name, lambda name=name: impl.trace.append((name,)))
        controller._on_rendering_change()
        controller._on_blending_change(object())
        controller.reset(object())
        assert impl.trace == [
            ("scalar.rendering",),
            ("_on_attenuation_change",),
            ("_on_iso_threshold_change",),
            ("scalar.blending",),
            ("_update_mip_minip_cutoff",),
            ("scalar.reset",),
            ("_on_interpolation_change",),
            ("_on_colormap_change",),
            ("_on_contrast_limits_change",),
            ("_on_gamma_change",),
        ]
        return tuple(impl.trace)

    _compare(implementations, scenario)


@pytest.mark.parametrize("normalized,span", [(False, 10.0), (True, 10.0), (True, 0.0)])
def test_contrast_cutoff_and_iso_threshold_order_and_zero_range_failure(
    implementations, normalized, span
):
    def scenario(impl):
        controller = _bare_controller(impl, volume=True)
        controller.node._texture.is_normalized = normalized
        controller.layer.contrast_limits_range = (0.0, span)
        error = _error(controller._on_contrast_limits_change)
        assert controller.node.clim == (2.0, 8.0)
        assert controller.node.mip_cutoff == 0.2 and controller.node.minip_cutoff == 0.8
        assert impl.trace[:4] == [
            ("contrast.coerce", (2.0, 8.0)),
            ("node.attr", "clim", (2.0, 8.0)),
            ("node.attr", "mip_cutoff", 0.2),
            ("node.attr", "minip_cutoff", 0.8),
        ]
        if span == 0:
            assert error is not None and error[0] == "ZeroDivisionError"
        else:
            assert controller.node.threshold == (0.4 if normalized else 4.0)
            assert error is None
        return error, tuple(impl.trace)

    _compare(implementations, scenario)


def test_volume_cutoff_blending_policy_and_attenuation_ignore_nonvolume(
    implementations,
):
    def scenario(impl):
        controller = _bare_controller(impl, volume=True)
        output = []
        for blending in (
            impl.blending.TRANSLUCENT,
            impl.blending.TRANSLUCENT_NO_DEPTH,
            impl.blending.OPAQUE,
        ):
            controller.layer.blending = blending
            controller._update_mip_minip_cutoff()
            output.append(
                (
                    str(blending),
                    controller.node.mip_cutoff,
                    controller.node.minip_cutoff,
                )
            )
        controller._on_attenuation_change()
        assert controller.node.attenuation == 0.3
        controller.node = impl.image(None, texture_format=None)
        impl.trace.clear()
        controller._on_attenuation_change()
        controller._on_iso_threshold_change()
        controller._update_mip_minip_cutoff()
        assert impl.trace == []
        return output

    _compare(implementations, scenario)


@pytest.mark.parametrize("kind", ["gray", "rgb", "strided"])
def test_make_tiles_preserves_numpy_views_offsets_and_row_major_order(
    implementations, kind
):
    def scenario(impl):
        data = (
            np.arange(60).reshape(5, 4, 3)
            if kind == "rgb"
            else np.arange(20).reshape(5, 4)
        )
        if kind == "strided":
            data = data[::-1, ::2]
        tiles = impl.tiles(data, 2)
        output = []
        for offset, tile in tiles:
            assert np.shares_memory(tile, data)
            x, y = offset
            np.testing.assert_array_equal(tile, data[y : y + 2, x : x + 2])
            output.append((offset, _value(tile), tile.strides))
        assert [offset for offset, _ in tiles] == [
            (x, y)
            for y in range(0, data.shape[0], 2)
            for x in range(0, data.shape[1], 2)
        ]
        return output

    _compare(implementations, scenario)


def test_tiling_empty_shape_and_zero_step_keep_original_errors(implementations):
    def scenario(impl):
        assert impl.tiles(np.zeros((0, 3)), 2) == []
        return _error(lambda: impl.tiles(np.zeros((2, 3)), 0)), _error(
            lambda: impl.tiles(np.zeros(3), 2)
        )

    _compare(implementations, scenario)


def test_tiled_node_initialization_data_alias_subvisuals_and_transforms(
    implementations,
):
    def scenario(impl):
        data = np.arange(15).reshape(3, 5)
        node = impl.tiled(data, 2, texture_format="r8")
        assert node.data is data
        assert all(
            a is b for a, b in zip(node.adopted_children, node._subvisuals, strict=True)
        )
        for child, offset in zip(node.adopted_children, node.offsets, strict=True):
            assert child.parent is node
            np.testing.assert_array_equal(child.transform.translate, (*offset, 0, 0))
            assert np.shares_memory(child._data, data)
        return node.offsets, tuple(impl.trace)

    _compare(implementations, scenario)


def test_tiled_equal_count_reuses_child_aliases_and_preserves_partial_upload(
    implementations,
):
    def scenario(impl):
        node = impl.tiled(np.arange(12).reshape(3, 4), 2)
        children = list(node.adopted_children)
        offsets = node.offsets
        data = np.arange(12, 24).reshape(3, 4)
        impl.trace.clear()
        impl.state.fail, impl.state.fail_at = "upload", 2
        error = _error(lambda: node.set_data(data))
        assert node.data is data and node.offsets is not offsets
        assert all(a is b for a, b in zip(children, node.adopted_children, strict=True))
        assert np.shares_memory(children[0]._data, data)
        assert not np.shares_memory(children[1]._data, data)
        return error, tuple(impl.trace), node.offsets

    _compare(implementations, scenario)


@pytest.mark.parametrize("failure", ["parent", "construct", "transform"])
def test_tile_replacement_failures_keep_detach_list_and_transform_publication_order(
    implementations, failure
):
    def scenario(impl):
        node = impl.tiled(np.arange(4).reshape(2, 2), 2)
        old_children = node.adopted_children
        data = np.arange(12).reshape(3, 4)
        impl.trace.clear()
        impl.state.fail, impl.state.fail_at = failure, impl.state.constructors + 2
        error = _error(lambda: node.set_data(data))
        assert error is not None and node.data is data
        if failure == "parent":
            assert (
                node.adopted_children is old_children and old_children[0].parent is node
            )
        elif failure == "construct":
            assert (
                node.adopted_children is old_children and old_children[0].parent is None
            )
            assert node._subvisuals == []
        else:
            assert node.adopted_children is not old_children and node._subvisuals == []
            assert old_children[0].parent is None
        return error, tuple(impl.trace), node.offsets, len(node.adopted_children)

    _compare(implementations, scenario)


def test_tiled_passthrough_aliases_gl_failure_and_missing_attribute(implementations):
    def scenario(impl):
        node = impl.tiled(np.arange(8).reshape(2, 4), 2)
        cmap = object()
        node.cmap = cmap
        assert node.cmap is cmap and all(
            child.cmap is cmap for child in node.adopted_children
        )
        assert node.events is node.adopted_children[0].events
        impl.trace.clear()
        impl.state.fail, impl.state.fail_at = "child.gl", 2
        error = _error(lambda: node.set_gl_state("translucent", blend=True))
        missing = _error(lambda: node.missing_attribute)
        assert error is not None and missing is not None
        node.adopted_children = []
        assert node.cmap is None and node.events is None
        return error, missing, tuple(impl.trace)

    _compare(implementations, scenario)


@pytest.mark.parametrize("extra", ["child", "offset"])
def test_axis_change_strict_zip_fails_after_existing_pair_transforms(
    implementations, extra
):
    def scenario(impl):
        node = impl.tiled(np.arange(4).reshape(2, 2), 2)
        if extra == "child":
            node.adopted_children.append(impl.backend.child(np.zeros((1, 1))))
        else:
            node.offsets.append((2, 0))
        impl.trace.clear()
        error = _error(node.handle_axis_change)
        assert error is not None and error[0] == "ValueError"
        assert impl.trace[0][0:2] == ("node.attr", "transform")
        return error, tuple(impl.trace)

    _compare(implementations, scenario)


def test_subvisual_add_and_transform_callback_preserve_child_transform_alias(
    implementations,
):
    def scenario(impl):
        node = impl.tiled(np.zeros((2, 2)), 2)
        child = impl.backend.child(np.zeros((1, 1)))
        transform = child.transform
        node.add_subvisual(child)
        impl.trace.clear()
        node._transform_changed(object())
        assert node._subvisuals[-1] is child and child.transform is transform
        assert impl.trace == [("base.transform_changed",)]
        return tuple(impl.trace)

    _compare(implementations, scenario)


def test_bounds_use_data_presence_axis_and_size_for_both_visuals(implementations):
    def scenario(impl):
        output = []
        for cls in (impl.image, impl.labels):
            node = cls(None, texture_format=None)
            assert node._compute_bounds(0, object()) is None
            node._data = np.zeros((3, 4))
            output.append(
                tuple(node._compute_bounds(axis, object()) for axis in (0, 1, 2))
            )
        return output

    _compare(implementations, scenario)


def test_label_color_transform_function_order_and_second_function_failure(
    implementations,
):
    def scenario(impl):
        node = impl.labels(np.zeros((3, 4)), texture_format=None)
        impl.trace.clear()
        result = node._build_color_transform()
        assert [f.code for f in result.funcs] == ["RED", "MAP"]
        good = tuple(impl.trace)
        impl.trace.clear()
        impl.state.fail = "function"
        error = _error(node._build_color_transform)
        assert impl.trace == [("function", "RED"), ("function", "MAP")]
        return good, error, tuple(impl.trace)

    _compare(implementations, scenario)


def test_volume_enum_shader_uniform_update_order_and_failure_publication(
    implementations,
):
    def scenario(impl):
        node = impl.volume(np.zeros((1, 1, 1)), texture_format=None)
        impl.trace.clear()
        node.iso_gradient_mode = "smooth"
        assert node.iso_gradient_mode == "smooth"
        assert (
            node.shared_program.frag
            == impl.namespaces["volume"]["SMOOTH_GRADIENT_SHADER"]
        )
        assert [item[0] for item in impl.trace] == [
            "program.attr",
            "program.item",
            "update",
        ]
        good = tuple(impl.trace)
        impl.trace.clear()
        invalid = _error(lambda: setattr(node, "iso_gradient_mode", "invalid"))
        assert node.iso_gradient_mode == "smooth" and impl.trace == []
        impl.state.fail = "program.u_clamp_at_border"
        failure = _error(lambda: setattr(node, "iso_gradient_mode", "fast"))
        assert node.iso_gradient_mode == "fast"
        assert (
            node.shared_program.frag
            == impl.namespaces["volume"]["FAST_GRADIENT_SHADER"]
        )
        assert not any(item[0] == "update" for item in impl.trace)
        return good, invalid, failure, tuple(impl.trace)

    _compare(implementations, scenario)


def test_clamp_setter_keeps_value_alias_before_uniform_failure(implementations):
    def scenario(impl):
        node = impl.volume(np.zeros((1, 1, 1)), texture_format=None)
        value = object()
        impl.trace.clear()
        impl.state.fail = "program.u_clamp_at_border"
        error = _error(lambda: setattr(node, "clamp_at_border", value))
        assert node.clamp_at_border is value
        assert len(impl.trace) == 1 and impl.trace[0][0] == "program.item"
        return error, tuple(impl.trace)

    _compare(implementations, scenario)


@pytest.mark.parametrize("kind", ["image", "labels", "volume", "tiled"])
def test_real_vispy_allocation_frozen_namespace_and_generated_node_cooperation(
    implementations, kind
):
    def scenario(impl):
        if kind == "volume":
            node = impl.volume(
                np.zeros((2, 2, 2), dtype=np.float32), texture_format=None
            )
        elif kind == "tiled":
            node = impl.tiled(np.arange(12, dtype=np.float32).reshape(3, 4), 2)
        else:
            node = getattr(impl, kind)(
                np.zeros((2, 3), dtype=np.float32), texture_format=None
            )
        assert isinstance(node, Frozen)
        namespace = vars(node)
        assert node.__dict__ is namespace
        assert "_Frozen__isfrozen" in namespace and namespace["_Frozen__isfrozen"]
        namespace["_contract_marker"] = "live"
        assert node._contract_marker == "live"
        error = _error(lambda: setattr(node, "_new_frozen_attribute", 7))
        assert error is not None and error[0] == "AttributeError"
        if kind == "labels":
            assert node._visual_superclass is impl.label_visual
            assert isinstance(node, impl.namespaces["labels"]["BaseLabel"])
        if kind == "tiled":
            assert len(node.adopted_children) == 4
        else:
            assert node.texture_format is None
            namespace["_texture_format"] = "saved_dictionary_format"
            assert node.texture_format is namespace["_texture_format"]
        return type(node).__name__, node._contract_marker, error[0]

    _compare(implementations, scenario, real=True)


def test_proposed_native_heap_base_cooperates_with_real_vispy_live_namespace(
    implementations,
):
    """One test-only M0 construction, not a mounted or implemented app facade."""
    original = implementations(PINNED, real=True)
    proposed = implementations(CURRENT, real=True, native_layout=True)

    def scenario(impl):
        output = []
        for kind in ("image", "labels", "volume", "tiled"):
            if kind == "volume":
                node = impl.volume(
                    np.zeros((2, 2, 2), dtype=np.float32), texture_format=None
                )
            elif kind == "tiled":
                node = impl.tiled(np.arange(12, dtype=np.float32).reshape(3, 4), 2)
            else:
                node = getattr(impl, kind)(
                    np.zeros((2, 3), dtype=np.float32), texture_format=None
                )
            if impl.root == CURRENT:
                native_base = impl.namespaces["util"]["_native"].VispyNamespaceBase
                assert isinstance(node, native_base)
            assert isinstance(node, Frozen)
            reference = weakref.ref(node)
            assert reference() is node
            saved = vars(node)
            assert node.__dict__ is saved
            transform = node.transform
            replacement = dict(saved)
            replacement["_namespace_marker"] = "replacement"
            replacement["transform"] = "descriptor_shadow"
            node.__dict__ = replacement
            assert vars(node) is replacement and node.__dict__ is replacement
            saved["_namespace_marker"] = "old_only"
            assert node._namespace_marker == "replacement"
            assert node.transform is transform
            if kind != "tiled":
                replacement["texture_format"] = "descriptor_shadow"
                replacement["_texture_format"] = "live_format"
                assert node.texture_format is replacement["_texture_format"]
            if kind == "labels":
                assert node._visual_superclass is impl.label_visual
            blocked = _error(
                lambda node=node: setattr(node, "_new_layout_attribute", 1)
            )
            assert blocked is not None and blocked[0] == "AttributeError"
            node.unfreeze()
            node._new_layout_attribute = 2
            node.freeze()
            assert replacement["_new_layout_attribute"] == 2
            assert replacement["_Frozen__isfrozen"]
            assert reference() is node
            output.append((kind, node._namespace_marker, blocked[0]))
        return output

    assert scenario(original) == scenario(proposed)


def test_clamp_descriptor_normalization_is_read_before_program_lookup(
    implementations,
):
    def scenario(impl):
        class NormalizedVolume(impl.volume):
            @property
            def _clamp_at_border(self):
                value = self._stored_border
                impl.trace.append(("border.read", value))
                return value

            @_clamp_at_border.setter
            def _clamp_at_border(self, value):
                impl.trace.append(("border.store", value))
                object.__setattr__(self, "_stored_border", not bool(value))

            @property
            def shared_program(self):
                impl.trace.append(("program.get",))
                return self._stored_program

            @shared_program.setter
            def shared_program(self, value):
                object.__setattr__(self, "_stored_program", value)

        node = NormalizedVolume(np.zeros((1, 1, 1)), texture_format=None)
        node.unfreeze()
        impl.trace.clear()
        impl.volume.clamp_at_border.fset(node, True)
        assert impl.trace == [
            ("border.store", True),
            ("border.read", False),
            ("program.get",),
            ("program.item", "u_clamp_at_border", False),
            ("update", "NormalizedVolume"),
        ]
        program = object.__getattribute__(node, "_stored_program")
        assert program["u_clamp_at_border"] is False
        return tuple(impl.trace)

    _compare(implementations, scenario)
