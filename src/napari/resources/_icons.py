from __future__ import annotations

from naparixx import _native

import re
from functools import lru_cache
from itertools import product
from pathlib import Path
from typing import TYPE_CHECKING

from napari.utils._platformdirs import user_cache_dir

if TYPE_CHECKING:
    from collections.abc import Iterable, Iterator

LOADING_GIF_PATH = str((Path(__file__).parent / 'loading.gif').resolve())
ICON_PATH = (Path(__file__).parent / 'icons').resolve()
_native.theme_registration_icons_discover(globals(), ('.svg',))
PLUGIN_FILE_NAME = 'plugin.txt'


def get_icon_path(name: str) -> str:
    """Return path to an SVG in the theme icons."""
    if name not in ICONS:
        raise ValueError(
            f'unrecognized icon name: {name!r}. Known names: {set(ICONS)}'
        )
    return ICONS[name]


svg_elem = re.compile(r'(<svg[^>]*>)')
svg_style = """<style type="text/css">
path {{fill: {0}; opacity: {1};}}
polygon {{fill: {0}; opacity: {1};}}
circle {{fill: {0}; opacity: {1};}}
rect {{fill: {0}; opacity: {1};}}
</style>"""


@lru_cache
def get_raw_svg(path: str) -> str:
    """Get and cache SVG XML."""
    locals_owner = _native.theme_registration_locals(8, path)
    del path
    return _native.theme_registration_run(
        8, globals(), locals_owner, (),
    )


@lru_cache
def get_colorized_svg(
    path_or_xml: str | Path, color: str | None = None, opacity=1.0
) -> str:
    """Return a colorized version of the SVG XML at ``path``.

    Raises
    ------
    ValueError
        If the path exists but does not contain valid SVG data.
    """
    locals_owner = _native.theme_registration_locals(9, path_or_xml, color, opacity)
    del path_or_xml, color, opacity
    return _native.theme_registration_run(
        9, globals(), locals_owner, ('</svg>', 'Could not detect svg tag in ', '\\1'),
    )


def generate_colorized_svgs(
    svg_paths: Iterable[str | Path],
    colors: Iterable[str | tuple[str, str]],
    opacities: Iterable[float] = (1.0,),
    theme_override: dict[str, str] | None = None,
) -> Iterator[tuple[str, str]]:
    """Helper function to generate colorized SVGs.

    This is a generator that yields tuples of ``(alias, icon_xml)`` for every
    combination (Cartesian product) of `svg_path`, `color`, and `opacity`
    provided. It can be used as input to :func:`_temporary_qrc_file`.

    Parameters
    ----------
    svg_paths : Iterable[Union[str, Path]]
        An iterable of paths to svg files
    colors : Iterable[Union[str, Tuple[str, str]]]
        An iterable of colors.  Every icon will be generated in every color. If
        a `color` item is a string, it should be valid svg color style. Items
        may also be a 2-tuple of strings, in which case the first item should
        be an available theme name
        (:func:`~napari.utils.theme.available_themes`), and the second item
        should be a key in the theme (:func:`~napari.utils.theme.get_theme`),
    opacities : Iterable[float], optional
        An iterable of opacities to generate, by default (1.0,) Opacities less
        than one can be accessed in qss with the opacity as a percentage
        suffix, e.g.: ``my_svg_50.svg`` for opacity 0.5.
    theme_override : Optional[Dict[str, str]], optional
        When one of the `colors` is a theme ``(name, key)`` tuple,
        `theme_override` may be used to override the `key` for a specific icon
        name in `svg_paths`.  For example ``{'exclamation': 'warning'}``, would
        use the theme "warning" color for any icon named "exclamation.svg" by
        default `None`

    Yields
    ------
    (alias, xml) : Iterator[Tuple[str, str]]
        `alias` is the name that will used to access the icon in the Qt
        Resource system (such as QSS), and `xml` is the *raw* colorzied SVG
        text (as read from a file, perhaps pre-colored using one of the below
        functions).
    """
    locals_owner = _native.theme_registration_locals(10, svg_paths, colors, opacities, theme_override)
    del svg_paths, colors, opacities, theme_override
    yield from _native.theme_registration_generator(
        globals(), locals_owner, ('{color}/{svg_stem}{opacity}.svg', ('napari.utils.theme', ('get_theme',), 0), 1, '', 100, '.0f', '_', ('color', 'svg_stem', 'opacity')),
    )


def write_colorized_svgs(
    dest: str | Path,
    svg_paths: Iterable[str | Path],
    colors: Iterable[str | tuple[str, str]],
    opacities: Iterable[float] = (1.0,),
    theme_override: dict[str, str] | None = None,
):
    locals_owner = _native.theme_registration_locals(11, dest, svg_paths, colors, opacities, theme_override)
    del dest, svg_paths, colors, opacities, theme_override
    return _native.theme_registration_run(
        11, globals(), locals_owner, (True, ('parents', 'exist_ok'), ('svg_paths', 'colors', 'opacities', 'theme_override')),
    )


def _theme_path(theme_name: str) -> Path:
    locals_owner = _native.theme_registration_locals(12, theme_name)
    del theme_name
    return _native.theme_registration_run(
        12, globals(), locals_owner, ('_themes',),
    )


def build_theme_svgs(theme_name: str, source) -> str:
    locals_owner = _native.theme_registration_locals(13, theme_name, source)
    del theme_name, source
    return _native.theme_registration_run(
        13, globals(), locals_owner, ('icon', (0.5, 1), ('svg_paths', 'colors', 'opacities', 'theme_override'), 'w', 'warning', 'error', 'logo_silhouette', 'foreground'),
    )
