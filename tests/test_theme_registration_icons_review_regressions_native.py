"""Original-first TR1 context, TR2 source leases and TR3 unpack contracts."""

# Each source callback finishes within its original/current family iteration.
# ruff: noqa: B023
from __future__ import annotations

import hashlib
import importlib.util
import sys
import weakref
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / ".artifacts/test_theme_registration_icons_next.py"
BASE_SHA = "ab87aacf08515d2000e38bc50333300c2721be17df2ff6f1d03a13081fddb625"
XML = '<svg xmlns="http://www.w3.org/2000/svg"><path d="M0 0"/></svg>'


@pytest.fixture(scope="module")
def regression_families():
    assert hashlib.sha256(BASE.read_bytes()).hexdigest() == BASE_SHA
    name = "_naparixx_registration_icons_review_base"
    assert name not in sys.modules
    spec = importlib.util.spec_from_file_location(name, BASE)
    assert spec is not None and spec.loader is not None
    base = importlib.util.module_from_spec(spec)
    sys.modules[name] = base
    generator = None
    try:
        spec.loader.exec_module(base)
        builder = base._execute

        def isolated_builder(name, *args, **kwargs):
            return builder(name + "_review_regressions", *args, **kwargs)

        base._execute = isolated_builder
        generator = base.families.__wrapped__()
        yield next(generator)
    finally:
        if generator is not None:
            generator.close()
        sys.modules.pop(name, None)


def test_source_exit_callable_survives_truth_and_prior_exception_restore(
    regression_families, monkeypatch
):
    for family in regression_families:
        trace = []
        references = []
        failure = RuntimeError("source marker write")
        prior = LookupError("previous handled exception")

        class Truth:
            def __bool__(self):
                trace.append(
                    ("truth", references[0]() is not None, sys.exception() is failure)
                )
                return True

        class Exit:
            def __init__(self):
                references.append(weakref.ref(self))

            def __call__(self, kind, error, traceback):
                trace.append(
                    (
                        "exit",
                        kind is RuntimeError,
                        error is failure,
                        traceback is not None,
                    )
                )
                del self, kind, error, traceback
                return Truth()

            def __del__(self):
                trace.append(("exit_released", sys.exception() is prior))

        class ExitDescriptor:
            def __get__(self, instance, owner):
                # This descriptor and the manager never retain the callable.
                del self, instance, owner
                return Exit()

        class File:
            __exit__ = ExitDescriptor()

            def __enter__(self):
                return self

            def write(self, source):
                del self, source
                raise failure

        class Marker:
            def open(self, mode):
                assert mode == "w"
                return File()

        class Out:
            def __truediv__(self, name):
                assert name == family.icons.PLUGIN_FILE_NAME
                return Marker()

            def __str__(self):
                trace.append("str")
                return "bounded.destination"

        def writer(*args, **kwargs):
            del args, kwargs

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "_theme_path", lambda name: Out())
            patch.setattr(family.icons, "write_colorized_svgs", writer)
            try:
                raise prior
            except LookupError:
                assert (
                    family.icons.build_theme_svgs("source", "plugin")
                    == "bounded.destination"
                )
                assert sys.exception() is prior
            assert trace == [
                ("exit", True, True, True),
                ("truth", True, True),
                ("exit_released", True),
                "str",
            ]
            assert references[0]() is None
            failure.__traceback__ = None
            prior.__traceback__ = None


def _lease_types(released, references):
    class Lease:
        def __init__(self, label):
            self.label = label
            references[label] = weakref.ref(self)

        def __del__(self):
            released.append(self.label)

    class LeaseDict(dict):
        def __init__(self, label):
            super().__init__()
            self.label = label
            references[label] = weakref.ref(self)

        def __del__(self):
            released.append(self.label)

    return Lease, LeaseDict


def test_source_generator_close_releases_parameters_before_current_aliases(
    regression_families, monkeypatch
):
    for family in regression_families:
        released, references = [], {}
        lease, _ = _lease_types(released, references)

        class RowValue(lease):
            def __fspath__(self):
                return "source.svg"

            def __str__(self):
                return "red"

            def __eq__(self, other):
                return self.label == "op" and other == 1

        class Rows(lease):
            def __iter__(self):
                return self

            def __next__(self):
                return RowValue("color"), RowValue("path"), RowValue("op")

        def product(*args):
            del args
            return Rows("iterator")

        def colorize(*args):
            del args
            return XML

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "product", product)
            patch.setattr(family.icons, "get_colorized_svg", colorize)
            generator = family.icons.generate_colorized_svgs(
                lease("svg_paths"),
                lease("colors"),
                lease("opacities"),
                lease("theme_override"),
            )
            assert released == []
            assert next(generator) == ("red/source.svg", XML)
            assert all(reference() is not None for reference in references.values())
            assert generator.close() is None
            # clrkey is the genuine second local reference to the color item.
            assert released == [
                "iterator",
                "svg_paths",
                "colors",
                "opacities",
                "theme_override",
                "path",
                "op",
                "color",
            ]
            assert all(reference() is None for reference in references.values())


def test_source_writer_releases_row_locals_before_call_arguments(
    regression_families, monkeypatch
):
    for family in regression_families:
        released, references = [], {}
        lease, _ = _lease_types(released, references)

        class Destination(lease):
            def mkdir(self, **kwargs):
                assert kwargs == {"parents": True, "exist_ok": True}

            def __truediv__(self, name):
                assert name == "name.svg"
                return Target()

        class Target:
            def write_text(self, svg):
                assert references["svg"]() is svg
                del self, svg

        class Rows(lease):
            done = False

            def __iter__(self):
                return self

            def __next__(self):
                if self.done:
                    raise StopIteration
                self.done = True
                return lease("alias"), lease("svg")

        def path(value):
            if isinstance(value, Destination):
                return value
            assert references["alias"]() is value
            return SimpleNamespace(name="name.svg")

        def rows(**kwargs):
            del kwargs
            return Rows("svgs")

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "Path", path)
            patch.setattr(family.icons, "generate_colorized_svgs", rows)
            assert (
                family.icons.write_colorized_svgs(
                    Destination("dest"),
                    lease("svg_paths"),
                    lease("colors"),
                    lease("opacities"),
                    lease("theme_override"),
                )
                is None
            )
            assert released == [
                "svgs",
                "alias",
                "svg",
                "dest",
                "svg_paths",
                "colors",
                "opacities",
                "theme_override",
            ]
            assert all(reference() is None for reference in references.values())


def test_source_builder_releases_output_file_before_call_arguments(
    regression_families, monkeypatch
):
    for family in regression_families:
        released, references = [], {}
        lease, _ = _lease_types(released, references)

        class File(lease):
            def __enter__(self):
                return self

            def write(self, source):
                assert references["source"]() is source
                del source

            def __exit__(self, kind, error, traceback):
                assert kind is None and error is None and traceback is None

        class Marker:
            def open(self, mode):
                assert mode == "w"
                return File("f")

        class Out(lease):
            def __truediv__(self, name):
                assert name == family.icons.PLUGIN_FILE_NAME
                return Marker()

            def __str__(self):
                return "bounded.destination"

        def theme_path(name):
            assert references["theme_name"]() is name
            del name
            return Out("out")

        def writer(*args, **kwargs):
            del args, kwargs

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "_theme_path", theme_path)
            patch.setattr(family.icons, "write_colorized_svgs", writer)
            assert (
                family.icons.build_theme_svgs(lease("theme_name"), lease("source"))
                == "bounded.destination"
            )
            assert released == ["out", "f", "theme_name", "source"]
            assert all(reference() is None for reference in references.values())


def test_source_installer_releases_entry_locals_before_call_argument(
    regression_families, monkeypatch
):
    for family in regression_families:
        released, references = [], {}
        lease, lease_dict = _lease_types(released, references)

        class BaseTheme:
            def model_dump(self):
                return lease_dict("theme_dict")

        class Themes(lease):
            def __getitem__(self, name):
                assert name == "dark"
                return BaseTheme()

        class Colors:
            def model_dump(self, **kwargs):
                assert kwargs == {"exclude_unset": True}
                return lease_dict("theme_colors")

        class Contribution(lease):
            type = "dark"
            id = "bounded.theme"

            @property
            def colors(self):
                return Colors()

            def model_dump(self, **kwargs):
                assert kwargs == {"exclude": {"colors"}, "exclude_unset": True}
                return lease_dict("theme_info")

        class OneRow:
            def __init__(self, factory):
                self.factory = factory
                self.done = False

            def __iter__(self):
                return self

            def __next__(self):
                if self.done:
                    raise StopIteration
                self.done = True
                return self.factory()

        class Manifest(lease):
            name = "bounded.plugin"

            @property
            def contributions(self):
                return SimpleNamespace(themes=OneRow(lambda: Contribution("theme")))

        class Manager:
            @staticmethod
            def instance():
                return Manager()

            def iter_manifests(self, **kwargs):
                assert kwargs == {"disabled": False}
                return OneRow(lambda: Manifest("manifest"))

        def register(*args):
            assert args[0] == "bounded.theme" and args[2] == "bounded.plugin"
            assert references["theme_dict"]() is args[1]
            del args

        with family.scope(monkeypatch) as patch:
            patch.setattr(sys.modules["npe2"], "PluginManager", Manager)
            patch.setattr(family.theme, "register_theme", register)
            assert family.theme._install_npe2_themes(Themes("themes")) is None
            # The imported real npe2 module remains owned by sys.modules.
            assert released == [
                "manifest",
                "theme",
                "theme_dict",
                "theme_info",
                "theme_colors",
                "themes",
            ]
            assert all(reference() is None for reference in references.values())


def test_source_saved_generator_error_survives_already_closed_throw(
    regression_families, monkeypatch
):
    for family in regression_families:
        references = []

        class SourcePath:
            def __init__(self):
                references.append(weakref.ref(self))

            def __fspath__(self):
                return "source.svg"

        class Paths:
            def __iter__(self):
                yield SourcePath()

        def colorize(*args):
            del args
            return XML

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "get_colorized_svg", colorize)
            generator = family.icons.generate_colorized_svgs(Paths(), ["red"], [1])
            assert next(generator) == ("red/source.svg", XML)
            thrown, later = RuntimeError("saved SVG error"), ValueError("closed throw")
            saved = None
            try:
                generator.throw(thrown)
            except RuntimeError as error:
                assert error is thrown
                saved = error
            assert saved is thrown and saved.__traceback__ is not None
            assert references[0]() is not None
            assert generator.close() is None
            try:
                generator.throw(later)
            except ValueError as error:
                assert error is later
            assert references[0]() is not None
            later.__traceback__ = None
            saved.__traceback__ = None
            assert references[0]() is None
            assert generator.close() is None


@pytest.mark.parametrize("mode", ["noniterable", "custom_iter_error"])
def test_source_three_unpack_diagnostic_and_custom_error_identity(
    regression_families, monkeypatch, mode
):
    for family in regression_families:
        calls = []
        failure = TypeError("custom iterator failure")

        class Broken:
            def __iter__(self):
                raise failure

        colors, paths, opacities = ["red"], ["source.svg"], [1]

        def product(*args):
            assert args[0] is colors and args[1] is paths and args[2] is opacities
            calls.append("product")
            return iter([3 if mode == "noniterable" else Broken()])

        with monkeypatch.context() as patch:
            patch.setattr(family.icons, "product", product)
            generator = family.icons.generate_colorized_svgs(paths, colors, opacities)
            assert calls == []
            with pytest.raises(TypeError) as caught:
                next(generator)
            if mode == "noniterable":
                assert type(caught.value) is TypeError
                assert caught.value.args == ("cannot unpack non-iterable int object",)
            else:
                assert caught.value is failure
                assert caught.value.args == ("custom iterator failure",)
            assert calls == ["product"]
            assert generator.close() is None
