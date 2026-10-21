"""Original-first theme registration, plugin recovery and SVG publication."""

# Source callbacks finish within each source-family iteration.
# ruff: noqa: B023
from __future__ import annotations

import ast
import hashlib
import json
import sys
import types
import weakref
from contextlib import ExitStack, contextmanager
from pathlib import Path
from types import ModuleType, SimpleNamespace
from xml.etree import ElementTree

import npe2
import pytest
from pydantic import ValidationError

from napari.resources import _icons as icons_module
from napari.settings import _appearance as appearance_module
from napari.utils import theme as theme_module

ROOT = Path(__file__).resolve().parents[1]
BEFORE = ROOT / ".artifacts/theme-registration-icons-before"
MANIFEST = ROOT / ".artifacts/theme-registration-icons-before-snapshots.json"
SELECTION = ROOT / ".artifacts/theme-next-owner-after-color-publication-selection.json"
SELECTION_SHA = "d245362a1c17ac3b96dddf270753a9d40bdd8f4fb788554eac3d2f62c59a9244"
THEME_NAMES = (
    "Theme",
    "register_theme",
    "unregister_theme",
    "is_theme_available",
    "rebuild_theme_settings",
    "_install_npe2_themes",
)
XML = '<svg xmlns="http://www.w3.org/2000/svg"><path d="M0 0"/></svg>'


def _function(tree, qualified):
    owner, _, name = qualified.rpartition(".")
    body = tree.body
    if owner:
        body = next(
            n.body for n in body if isinstance(n, ast.ClassDef) and n.name == owner
        )
    return next(n for n in body if isinstance(n, ast.FunctionDef) and n.name == name)


def _execute(name, raw, filename, namespace, nodes=None):
    module = ModuleType(name)
    module.__file__ = str(filename)
    module.__dict__.update(
        {
            key: value
            for key, value in namespace.items()
            if key
            not in ("__name__", "__file__", "__spec__", "__loader__", "__annotations__")
        }
    )
    assert name not in sys.modules
    sys.modules[name] = module
    try:
        tree = (
            ast.parse(raw)
            if nodes is None
            else ast.fix_missing_locations(ast.Module(nodes, []))
        )
        exec(compile(tree, str(filename), "exec", dont_inherit=True), module.__dict__)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


class Family:
    def __init__(self, name, theme, icons, refresh):
        self.name, self.theme, self.icons, self.refresh = name, theme, icons, refresh
        self.Theme = theme.Theme

    @contextmanager
    def scope(self, monkeypatch):
        with monkeypatch.context() as patch, ExitStack() as stack:
            if self.name == "original":
                patch.setattr(
                    appearance_module.AppearanceSettings, "refresh_themes", self.refresh
                )
                for event in (
                    theme_module._themes.events.added,
                    theme_module._themes.events.removed,
                ):
                    stack.enter_context(
                        event.blocker(theme_module.rebuild_theme_settings)
                    )
                    event.connect(self.theme.rebuild_theme_settings)
                    stack.callback(event.disconnect, self.theme.rebuild_theme_settings)
            yield patch


@pytest.fixture(scope="module")
def families():
    assert hashlib.sha256(SELECTION.read_bytes()).hexdigest() == SELECTION_SHA
    for row in json.loads(MANIFEST.read_bytes())["files"]:
        item = row["snapshot"]
        raw = (ROOT / item["path"]).read_bytes()
        assert (
            len(raw) == item["bytes"]
            and hashlib.sha256(raw).hexdigest() == item["sha256"]
        )
    for row in json.loads(SELECTION.read_bytes())["selected_bodies"]:
        source = row["source"]
        current = _function(
            ast.parse((BEFORE / source).read_bytes()), row["qualified_name"]
        )
        pinned = _function(
            ast.parse((BEFORE / ".upstream/napari" / source).read_bytes()),
            row["qualified_name"],
        )
        assert ast.dump(current, include_attributes=False) == ast.dump(
            pinned, include_attributes=False
        )
    modules = []
    try:
        icons = _execute(
            "_naparixx_registration_icons_original",
            (BEFORE / "src/napari/resources/_icons.py").read_bytes(),
            icons_module.__file__,
            {},
        )
        modules.append(icons)
        path = BEFORE / "src/napari/utils/theme.py"
        tree = ast.parse(path.read_bytes())
        nodes = [
            node
            for node in tree.body
            if isinstance(node, (ast.ClassDef, ast.FunctionDef))
            and node.name in THEME_NAMES
        ]
        declaration = next(n for n in nodes if isinstance(n, ast.ClassDef))
        # Legal original validator overrides pass the canonical registry's
        # genuine Theme basetype; method bodies/decorators/cells remain exact.
        declaration.bases = [ast.Name("ActualTheme", ast.Load())]
        namespace = {
            **theme_module.__dict__,
            "ActualTheme": theme_module.Theme,
            "build_theme_svgs": icons.build_theme_svgs,
            "_theme_path": icons._theme_path,
        }
        theme = _execute(
            "_naparixx_registration_theme_original", b"", path, namespace, nodes
        )
        modules.append(theme)
        for name in ("_ensure_syntax_style", "_ensure_font_size"):
            callback = theme.Theme.__pydantic_decorators__.field_validators[name].func
            assert callback.__func__ is getattr(theme.Theme, name).__func__
            assert callback.__func__.__code__.co_filename == str(path)
        refresh = _function(
            ast.parse((BEFORE / "src/napari/settings/_appearance.py").read_bytes()),
            "AppearanceSettings.refresh_themes",
        )
        wrapper = ast.ClassDef(
            name="AppearanceSettings",
            bases=[],
            keywords=[],
            body=[refresh],
            decorator_list=[],
            type_params=[],
        )
        ast.copy_location(wrapper, refresh)
        appearance = _execute(
            "_naparixx_registration_appearance_original",
            b"",
            BEFORE / "src/napari/settings/_appearance.py",
            appearance_module.__dict__,
            [wrapper],
        )
        modules.append(appearance)
        assert theme._themes is theme_module._themes
        yield (
            Family(
                "original", theme, icons, appearance.AppearanceSettings.refresh_themes
            ),
            Family(
                "current",
                theme_module,
                icons_module,
                appearance_module.AppearanceSettings.refresh_themes,
            ),
        )
    finally:
        for module in modules:
            sys.modules.pop(module.__name__, None)


def _svgs(tmp_path):
    directory = tmp_path / "inputs"
    directory.mkdir(exist_ok=True)
    paths = [directory / "warning.svg", directory / "a.svg"]
    for path in paths:
        path.write_text(XML)
    return paths


@contextmanager
def _small_icons(module, paths):
    table = module.ICONS
    old = list(table.items())
    table.clear()
    table.update((path.stem, str(path)) for path in paths)
    try:
        yield table
    finally:
        table.clear()
        table.update(old)


@pytest.mark.parametrize(
    "mode", ["valid_rgb", "syntax_error", "font_suffix", "font_zero"]
)
def test_original_theme_validators_pydantic_and_lexical_super(families, mode):
    data = theme_module.get_theme("dark").model_dump()
    for family in families:
        if mode == "valid_rgb":
            model = family.Theme(**data)
            result = model.to_rgb_dict()
            assert (
                result["id"] == data["id"] and result["canvas"] == model.canvas.as_rgb()
            )
            assert result["font_size"] == model.font_size
        elif mode == "syntax_error":
            with pytest.raises(ValidationError, match="Incorrect `syntax_style` value"):
                family.Theme(**{**data, "syntax_style": "_missing_style_"})
        else:
            message = (
                "Font size must be in points"
                if mode == "font_suffix"
                else "Font size must be greater than 0"
            )
            with pytest.raises(ValidationError, match=message):
                family.Theme(
                    **{**data, "font_size": "12px" if mode == "font_suffix" else "0pt"}
                )


@pytest.mark.parametrize("mode", ["dict", "instance", "invalid_dict", "svg_failure"])
def test_original_registration_canonical_events_identity_and_io_prefix(
    families, monkeypatch, tmp_path, mode
):
    paths = _svgs(tmp_path)
    registry = theme_module._themes
    for family in families:
        name = "_naparixx_registered_source_"
        assert name not in registry
        data = {
            **theme_module.get_theme("dark").model_dump(),
            "id": name,
            "label": "Source",
        }
        if mode == "invalid_dict":
            data["font_size"] = "0pt"
        supplied = family.Theme(**data) if mode == "instance" else data
        trace = []
        failure = RuntimeError("SVG publisher failed")

        def event_changed(event):
            trace.append((event.type, name in registry))

        def fail(theme_id, source):
            trace.append(("build", theme_id in registry, source))
            raise failure

        with family.scope(monkeypatch) as patch, _small_icons(family.icons, paths):
            patch.setattr(
                family.icons, "user_cache_dir", lambda: str(tmp_path / family.name)
            )
            if mode == "svg_failure":
                patch.setattr(family.theme, "build_theme_svgs", fail)
            registry.events.added.connect(event_changed)
            registry.events.removed.connect(event_changed)
            try:
                if mode == "invalid_dict":
                    with pytest.raises(ValidationError):
                        family.theme.register_theme(name, supplied, "source.marker")
                    assert name not in registry and trace == []
                else:
                    if mode == "svg_failure":
                        with pytest.raises(RuntimeError) as caught:
                            family.theme.register_theme(name, supplied, "source.marker")
                        assert caught.value is failure
                    else:
                        assert (
                            family.theme.register_theme(name, supplied, "source.marker")
                            is None
                        )
                        out = family.icons._theme_path(name)
                        assert (
                            out / family.icons.PLUGIN_FILE_NAME
                        ).read_text() == "source.marker"
                        assert {p.name for p in out.glob("*.svg")} == {
                            "warning.svg",
                            "warning_50.svg",
                            "a.svg",
                            "a_50.svg",
                        }
                        for path in out.glob("*.svg"):
                            ElementTree.fromstring(path.read_text())
                    assert family.theme._themes is registry and isinstance(
                        registry[name], family.Theme
                    )
                    if mode == "instance":
                        assert registry[name] is supplied
                    assert trace[0] == ("added", True)
                    family.theme.unregister_theme(name)
                    assert trace[-1] == ("removed", False)
                    assert family.theme.unregister_theme(name) is None
            finally:
                if name in registry:
                    registry.pop(name)
                registry.events.added.disconnect(event_changed)
                registry.events.removed.disconnect(event_changed)


def test_original_refresh_updates_returned_schema_enum_by_identity(
    families, monkeypatch
):
    for family in families:
        enum = ["first", "system"]
        properties = {"old": object()}
        schema = {"properties": {"theme": properties}}
        with monkeypatch.context() as patch:
            namespace = family.refresh.__globals__
            patch.setitem(namespace, "available_themes", lambda: enum)
            receiver = SimpleNamespace(model_json_schema=lambda: schema)
            assert family.refresh(receiver) is None
        assert properties["enum"] is enum
        assert "old" in properties


@pytest.mark.parametrize(
    "mode",
    ["system", "missing_dir", "no_marker", "plugin_import_missing", "plugin_recovery"],
)
def test_original_availability_files_plugin_suppression_and_recovery(
    families, monkeypatch, tmp_path, mode
):
    registry = theme_module._themes
    paths = _svgs(tmp_path)
    for family in families:
        name = "system" if mode == "system" else "_naparixx_recovery_source_"
        assert mode == "system" or name not in registry
        trace = []
        root = tmp_path / family.name / mode
        folder = root / "_themes" / name
        if mode not in ("system", "missing_dir"):
            folder.mkdir(parents=True)
        if mode.startswith("plugin_"):
            (folder / family.icons.PLUGIN_FILE_NAME).write_text("demo.source")

        class Contribution:
            type = "dark"
            id = name
            colors = SimpleNamespace(model_dump=lambda **kwargs: {"icon": "red"})

            def model_dump(self, **kwargs):
                assert kwargs == {"exclude": {"colors"}, "exclude_unset": True}
                return {"id": name, "label": "Recovered", "type": "dark"}

        manifest = SimpleNamespace(
            name="demo.source", contributions=SimpleNamespace(themes=[Contribution()])
        )

        class Manager:
            def register(self, plugin):
                trace.append(("register", plugin))
                if mode == "plugin_import_missing":
                    raise ModuleNotFoundError("missing optional plugin")

            def iter_manifests(self, **kwargs):
                trace.append(("manifests", kwargs))
                return [manifest] if mode == "plugin_recovery" else []

        manager = Manager()
        with family.scope(monkeypatch) as patch, _small_icons(family.icons, paths):
            patch.setattr(family.icons, "user_cache_dir", lambda: str(root))
            patch.setattr(
                npe2, "PluginManager", SimpleNamespace(instance=lambda: manager)
            )
            try:
                result = family.theme.is_theme_available(name)
                assert result is (mode in ("system", "plugin_recovery"))
                assert trace == (
                    [("register", "demo.source"), ("manifests", {"disabled": False})]
                    if mode.startswith("plugin_")
                    else []
                )
                if mode == "plugin_recovery":
                    assert registry[name].label == "Recovered"
                    assert (
                        folder / family.icons.PLUGIN_FILE_NAME
                    ).read_text() == "demo.source"
            finally:
                if mode != "system" and name in registry:
                    family.theme.unregister_theme(name)


def test_original_plugin_install_logs_invalid_then_registers_valid(
    families, monkeypatch, tmp_path
):
    paths = _svgs(tmp_path)
    registry = theme_module._themes
    for family in families:
        logs = []
        keys = ["_naparixx_invalid_plugin_", "_naparixx_valid_plugin_"]

        class Contribution:
            type = "dark"
            colors = SimpleNamespace(model_dump=lambda **kwargs: {"icon": "red"})

            def __init__(self, index):
                self.index, self.id = index, keys[index]

            def model_dump(self, **kwargs):
                result = {"id": self.id, "label": "Valid", "type": "dark"}
                if self.index == 0:
                    result["font_size"] = "0pt"
                return result

        manifest = SimpleNamespace(
            name="plugin.source",
            contributions=SimpleNamespace(themes=[Contribution(0), Contribution(1)]),
        )
        manager = SimpleNamespace(iter_manifests=lambda **kwargs: [manifest])
        logger = SimpleNamespace(exception=lambda message: logs.append(message))
        with family.scope(monkeypatch) as patch, _small_icons(family.icons, paths):
            patch.setattr(
                family.icons, "user_cache_dir", lambda: str(tmp_path / family.name)
            )
            patch.setattr(
                npe2, "PluginManager", SimpleNamespace(instance=lambda: manager)
            )
            patch.setattr(
                family.theme, "logging", SimpleNamespace(getLogger=lambda name: logger)
            )
            try:
                assert family.theme._install_npe2_themes() is None
                assert keys[0] not in registry and keys[1] in registry
                assert logs == ["Registration theme failed."]
            finally:
                for key in keys:
                    if key in registry:
                        family.theme.unregister_theme(key)


@pytest.mark.parametrize("mode", ["file_color", "falsey_raw", "invalid_markup"])
def test_original_svg_xml_regex_and_error_prefix(families, tmp_path, mode):
    path = _svgs(tmp_path)[0]
    observations = []
    for family in families:
        if mode == "file_color":
            value = family.icons.get_colorized_svg(path, "red", 0.5)
            parsed = ElementTree.fromstring(value)
            assert any(node.tag.endswith("style") for node in parsed)
            assert "fill: red; opacity: 0.5;" in value
        elif mode == "falsey_raw":
            value = family.icons.get_colorized_svg(XML, "", 0.25)
            assert value == XML
        else:
            raw = "not an svg </svg>"
            with pytest.raises(ValueError) as caught:
                family.icons.get_colorized_svg(raw, "red")
            value = str(caught.value)
            assert value == f"Could not detect svg tag in {raw!r}"
        observations.append(value)
    assert observations[1] == observations[0]


def test_original_bare_lru_defaults_raw_alias_wrapped_and_clear(families, tmp_path):
    for family in families:
        path = tmp_path / (family.name + ".svg")
        path.write_text(XML)
        raw, color = family.icons.get_raw_svg, family.icons.get_colorized_svg
        assert (
            raw.cache_parameters()
            == color.cache_parameters()
            == {"maxsize": 128, "typed": False}
        )
        first = raw(str(path))
        before = raw.cache_info().hits
        path.write_text(XML.replace("M0 0", "M1 1"))
        assert raw(str(path)) is first and raw.cache_info().hits == before + 1
        assert raw.__wrapped__(str(path)) != first
        raw.cache_clear()
        assert raw.cache_info().currsize == 0
        assert "M1 1" in raw(str(path))
        color.cache_clear()
        assert color.cache_info().currsize == 0
        assert color(path, None) == raw(str(path))


def test_original_svg_generator_complete_product_order_and_flattened_writes(
    families, tmp_path
):
    paths = _svgs(tmp_path)
    observations = []
    for family in families:
        rows = list(
            family.icons.generate_colorized_svgs(
                paths, ["red", ("dark", "icon")], [0.5, 1], {"warning": "warning"}
            )
        )
        assert len(rows) == 8
        assert [row[0] for row in rows] == [
            f"{color}/{path.stem}{opacity}.svg"
            for color in ("red", "dark")
            for path in paths
            for opacity in ("_50", "")
        ]
        for _, xml in rows:
            ElementTree.fromstring(xml)
        destination = tmp_path / family.name / "written"
        assert (
            family.icons.write_colorized_svgs(destination, paths, ["red"], [0.5, 1])
            is None
        )
        assert {p.name for p in destination.iterdir()} == {
            "warning.svg",
            "warning_50.svg",
            "a.svg",
            "a_50.svg",
        }
        observations.append(rows)
    assert observations[1] == observations[0]


@pytest.mark.parametrize("mode", ["close", "throw"])
def test_original_generator_lazy_product_close_throw_and_error_roots(
    families, monkeypatch, tmp_path, mode
):
    path = _svgs(tmp_path)[0]
    for family in families:
        references = []
        trace = []

        class SourcePath:
            def __init__(self):
                references.append(weakref.ref(self))

            def __fspath__(self):
                return str(path)

            def __del__(self):
                trace.append("released")

        class Paths:
            def __iter__(self):
                trace.append("iter")
                yield SourcePath()

        def colorize(path, color, opacity):
            trace.append("colorize")
            return XML

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "get_colorized_svg", colorize)
            generator = family.icons.generate_colorized_svgs(Paths(), ["red"], [1])
            assert (
                isinstance(generator, types.GeneratorType)
                and iter(generator) is generator
            )
            assert trace == [] and references == []
            assert next(generator) == ("red/warning.svg", XML)
            assert trace == ["iter", "colorize"] and references[0]() is not None
            if mode == "close":
                assert generator.close() is None
                assert references[0]() is None
            else:
                thrown = RuntimeError("injected source error")
                saved = None
                try:
                    generator.throw(thrown)
                except RuntimeError as error:
                    assert error is thrown
                    saved = error
                assert saved is thrown and saved.__traceback__ is not None
                assert references[0]() is not None
                generator.close()
                assert references[0]() is not None
                saved.__traceback__ = None
                assert references[0]() is None
            assert generator.close() is None


@pytest.mark.parametrize("suppress", [False, True], ids=["propagate", "suppress"])
def test_original_marker_context_error_and_suppression(families, monkeypatch, suppress):
    for family in families:
        trace = []
        failure = RuntimeError("marker write failed")

        class File:
            def __enter__(self):
                trace.append("enter")
                return self

            def write(self, value):
                trace.append(("write", value))
                raise failure

            def __exit__(self, kind, error, tb):
                trace.append(
                    ("exit", kind is RuntimeError, error is failure, tb is not None)
                )
                return suppress

        class Marker:
            def open(self, mode):
                trace.append(("open", mode))
                return File()

        class Out:
            def __truediv__(self, name):
                assert name == family.icons.PLUGIN_FILE_NAME
                return Marker()

            def __str__(self):
                trace.append("str")
                return "bounded.destination"

        def writer(out, **kwargs):
            trace.append("svgs")
            assert kwargs["colors"] == [("source", "icon")]
            assert kwargs["opacities"] == (0.5, 1)

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "_theme_path", lambda name: Out())
            patch.setattr(family.icons, "write_colorized_svgs", writer)
            if suppress:
                assert (
                    family.icons.build_theme_svgs("source", "plugin")
                    == "bounded.destination"
                )
            else:
                with pytest.raises(RuntimeError) as caught:
                    family.icons.build_theme_svgs("source", "plugin")
                assert caught.value is failure
        assert trace == [
            "svgs",
            ("open", "w"),
            "enter",
            ("write", "plugin"),
            ("exit", True, True, True),
        ] + (["str"] if suppress else [])


def test_original_icons_initialization_discovers_real_paths_and_filter_order(tmp_path):
    directory = tmp_path / "resources"
    icon_dir = directory / "icons"
    icon_dir.mkdir(parents=True)
    for name in ("a.svg", "skip.png", "b.svg", "upper.SVG"):
        (icon_dir / name).write_text(XML)
    (icon_dir / "directory.svg").mkdir()
    observations = []
    sources = [
        ("original", (BEFORE / "src/napari/resources/_icons.py").read_bytes()),
        ("current", Path(icons_module.__file__).read_bytes()),
    ]
    for label, raw in sources:
        name = "_naparixx_icon_discovery_" + label
        module = _execute(name, raw, directory / "_icons.py", {})
        try:
            assert isinstance(module.ICONS, dict)
            assert set(module.ICONS) == {"a", "b", "directory"}
            assert module.ICON_PATH == icon_dir.resolve()
            assert module.LOADING_GIF_PATH == str((directory / "loading.gif").resolve())
            observations.append(list(module.ICONS.items()))
        finally:
            sys.modules.pop(name, None)
    assert observations[1] == observations[0]
