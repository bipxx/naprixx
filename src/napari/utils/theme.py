# syntax_style for the console must be one of the supported styles from
# pygments - see here for examples https://help.farbox.com/pygments.html
import logging
import re
import sys
from ast import literal_eval
from contextlib import suppress
from typing import Any, Literal
from warnings import warn

from naparixx import _native

from naparixx import _color_policy as _color_policy_metadata

_color_policy_metadata.activate()

import npe2
from pydantic import field_validator
from pydantic_extra_types.color import Color

from napari.resources._icons import (
    PLUGIN_FILE_NAME,
    _theme_path,
    build_theme_svgs,
)
from napari.utils.events import EventedModel
from napari.utils.events.containers._evented_dict import EventedDict


# Immutable native binding and inherited callable metadata; no old Theme class.
from functools import wraps as _theme_source_wraps
from types import FunctionType as _theme_constructor_function_type
from pydantic import main as _theme_pydantic_main
from napari.utils.events import evented_model as _theme_evented_model

_theme_original_class = EventedModel
_theme_state_binding = _native.theme_state_bindings(
    _theme_pydantic_main.BaseModel,
    vars(_theme_pydantic_main),
    vars(_theme_evented_model),
    ('memo',),
)
_ThemeNamespaceBase = _theme_state_binding.base


# Constructor declaration glue only: genuine lexical super and thin frame envelopes.
def _theme_constructor_super_metadata(__class__):
    def _source_super(self):
        return super()
    return _source_super


_theme_constructor_super_template = _theme_constructor_super_metadata(EventedModel)
_theme_constructor_super = _theme_constructor_function_type(
    _theme_constructor_super_template.__code__,
    vars(_theme_evented_model),
    _theme_constructor_super_template.__name__,
    _theme_constructor_super_template.__defaults__,
    _theme_constructor_super_template.__closure__,
)
del _theme_constructor_super_template


def _theme_constructor_location(source, line_offset):
    def decorate(function):
        code = function.__code__
        last_line = max(line for _, _, line in code.co_lines() if line is not None)
        code = code.replace(
            co_filename=source.__code__.co_filename,
            co_firstlineno=code.co_firstlineno + source.__code__.co_firstlineno
            + line_offset - last_line,
            co_name=source.__code__.co_name,
            co_qualname=source.__code__.co_qualname,
        )
        return _theme_constructor_function_type(
            code, source.__globals__, function.__name__,
            function.__defaults__, function.__closure__,
        )
    return decorate


def _theme_constructor_base_metadata(binding):
    @_theme_source_wraps(_theme_pydantic_main.BaseModel.__init__)
    @_theme_constructor_location(_theme_pydantic_main.BaseModel.__init__, 10)
    def __init__(self, /, **data):
        _source_locals = binding.init_locals(1, self, data)
        del self, data
        return binding.init_run(_source_locals)
    return __init__


_theme_constructor_base = _theme_constructor_base_metadata(_theme_state_binding)


def _theme_constructor_evented_metadata(binding, super_provider, base_provider):
    @_theme_source_wraps(_theme_original_class.__init__)
    @_theme_constructor_location(_theme_original_class.__init__, 1)
    def __init__(self, **kwargs) -> None:
        _source_locals = binding.init_locals(
            0, self, kwargs, super_provider, base_provider,
        )
        del self, kwargs
        return binding.init_run(_source_locals)
    return __init__


# Assignment declaration envelope only; native code owns dispatch/memoization.
def _theme_assignment_base_metadata(binding):
    @_theme_source_wraps(_theme_pydantic_main.BaseModel.__setattr__)
    @_theme_constructor_location(_theme_pydantic_main.BaseModel.__setattr__, 6)
    def __setattr__(self, name: str, value: Any) -> None:
        _source_locals = binding.assignment_locals(self, name, value)
        del self, name, value
        return binding.assignment_run(_source_locals)
    return __setattr__


# Selector declaration envelopes; native code owns all selection and invocation.
def _theme_selector_base_metadata(binding):
    @_theme_source_wraps(_theme_pydantic_main.BaseModel._setattr_handler)
    @_theme_constructor_location(_theme_pydantic_main.BaseModel._setattr_handler, 53)
    def _setattr_handler(self, name: str, value: Any):
        _source_locals = binding.selector_locals(self, name, value)
        del self, name, value
        return binding.selector_run(_source_locals)
    return _setattr_handler


def _theme_selector_descriptor_code(function, source_code, native_frame):
    code = function.__code__
    last_line = max(line for _, _, line in code.co_lines() if line is not None)
    constants = tuple(
        native_frame if value is Ellipsis else value for value in code.co_consts
    )
    code = code.replace(
        co_consts=constants,
        co_filename=source_code.co_filename,
        co_firstlineno=code.co_firstlineno + source_code.co_firstlineno - last_line,
        co_name=source_code.co_name,
        co_qualname=source_code.co_qualname,
    )
    return _theme_constructor_function_type(
        code, vars(_theme_pydantic_main), source_code.co_name,
        function.__defaults__, function.__closure__,
    )


def _theme_selector_private_descriptor_metadata(attribute, source_code, native_frame):
    def setter(model, _name, val):
        _source_locals = (...).__call__(attribute, model, _name, val)
        del model, _name, val
        return (...).__call__(_source_locals)
    return _theme_selector_descriptor_code(setter, source_code, native_frame)


def _theme_selector_property_descriptor_metadata(attr, source_code, native_frame):
    def setter(model, _name, val):
        _source_locals = (...).__call__(attr, model, _name, val)
        del model, _name, val
        return (...).__call__(_source_locals)
    return _theme_selector_descriptor_code(setter, source_code, native_frame)


# Validation-handler declaration frame; the original public mapping stays intact.
def _theme_validate_assignment_metadata(source_code, native_frame):
    def handler(model, name, val):
        _source_locals = (...).__call__(model, name, val)
        del model, name, val
        return (...).__call__(_source_locals)
    return _theme_selector_descriptor_code(handler, source_code, native_frame)


# Simple-handler declaration envelopes retain actual source namespaces only.
def _theme_simple_assignment_metadata(
    source_code, native_frame, source_globals, source_builtins,
):
    def handler(model, name, val):
        _source_locals = (...).__call__(
            source_globals, source_builtins, model, name, val,
        )
        del model, name, val
        return (...).__call__(_source_locals)
    code = handler.__code__
    last_line = max(line for _, _, line in code.co_lines() if line is not None)
    source_last_line = max(
        line for _, _, line in source_code.co_lines() if line is not None
    )
    constants = tuple(
        native_frame if value is Ellipsis else value for value in code.co_consts
    )
    code = code.replace(
        co_consts=constants,
        co_filename=source_code.co_filename,
        co_firstlineno=code.co_firstlineno + source_last_line - last_line,
        co_name=source_code.co_name,
        co_qualname=source_code.co_qualname,
    )
    return _theme_constructor_function_type(
        code, source_globals, source_code.co_name,
        handler.__defaults__, handler.__closure__,
    )


# Owned-policy declaration envelopes only; native code owns both bodies.
def _theme_selector_policy_metadata(
    source_code, native_frame, source_globals, source_builtins,
):
    def is_valid_field_name(name):
        _source_locals = (...).__call__(source_globals, source_builtins, name)
        del name
        return (...).__call__(_source_locals)

    def _check_frozen(model_cls, name, value):
        _source_locals = (...).__call__(
            source_globals, source_builtins, model_cls, name, value,
        )
        del model_cls, name, value
        return (...).__call__(_source_locals)

    declaration = is_valid_field_name if source_code.co_argcount == 1 else _check_frozen
    code = declaration.__code__
    last_line = max(line for _, _, line in code.co_lines() if line is not None)
    source_last_line = max(
        line for _, _, line in source_code.co_lines() if line is not None
    )
    constants = tuple(
        native_frame if value is Ellipsis else value for value in code.co_consts
    )
    code = code.replace(
        co_consts=constants,
        co_filename=source_code.co_filename,
        co_firstlineno=code.co_firstlineno + source_last_line - last_line,
        co_name=source_code.co_name,
        co_qualname=source_code.co_qualname,
    )
    return _theme_constructor_function_type(
        code, source_globals, source_code.co_name,
        declaration.__defaults__, declaration.__closure__,
    )


class Theme(_ThemeNamespaceBase, EventedModel):
    """Theme model.

    Attributes
    ----------
    id : str
        id of the theme and name of the virtual folder where icons
        will be saved to.
    label : str
        Name of the theme as it should be shown in the ui.
    type: str
        Whether the theme is "dark" or "light" type.
    syntax_style : str
        Name of the console style.
        See for more details: https://pygments.org/docs/styles/
    canvas : Color
        Background color of the canvas.
    background : Color
        Color of the application background.
    foreground : Color
        Color to contrast with the background.
    primary : Color
        Color used to make part of a widget more visible.
    secondary : Color
        Alternative color used to make part of a widget more visible.
    highlight : Color
        Color used to highlight visual element.
    text : Color
        Color used to display text.
    warning : Color
        Color used to indicate something needs attention.
    error : Color
        Color used to indicate something is wrong or could stop functionality.
    current : Color
        Color used to highlight Qt widget.
    font_size : str
        Font size (in points, pt) used in the application.
    """

    id: str
    label: str
    type: Literal['dark', 'light']
    syntax_style: str
    canvas: Color
    console: Color
    background: Color
    foreground: Color
    primary: Color
    secondary: Color
    highlight: Color
    text: Color
    icon: Color
    warning: Color
    error: Color
    current: Color
    # base font sizes differ between platforms
    # macOS uses 72 dpi while windows and linux use 96
    # which is a factor of 4/3 so 12 and 9 should be similar
    font_size: str = '12pt' if sys.platform == 'darwin' else '9pt'

    @field_validator('syntax_style', mode='before')
    @classmethod
    def _ensure_syntax_style(cls, value: str) -> str:
        locals_owner = _native.theme_registration_locals(0, cls, value)
        del cls, value
        return _native.theme_registration_run(
            0, globals(), locals_owner, (('pygments.styles', ('STYLE_MAP',), 0), ', ', __debug__, 'Incorrect `syntax_style` value: ', ' provided. Please use one of the following: '),
        )

    @field_validator('font_size', mode='before')
    @classmethod
    def _ensure_font_size(cls, value: str) -> str:
        locals_owner = _native.theme_registration_locals(1, cls, value)
        del cls, value
        return _native.theme_registration_run(
            1, globals(), locals_owner, (__debug__, 'pt', 'Font size must be in points (pt).', -2, 0, 'Font size must be greater than 0.'),
        )

    def to_rgb_dict(self) -> dict[str, Any]:
        """
        This differs from baseclass `dict()` by converting colors to rgb.
        """
        super_provider = lambda _receiver=self: super()
        locals_owner = _native.theme_registration_locals(2, self)
        del self
        return _native.theme_registration_run(
            2, globals(), locals_owner, (), super_provider,
        )


    __init__ = _theme_constructor_evented_metadata(
        _theme_state_binding, _theme_constructor_super, _theme_constructor_base,
    )

    @_theme_source_wraps(_theme_original_class.__copy__)
    def __copy__(self):
        _source_locals = _theme_state_binding.locals(0, self)
        del self
        return _theme_state_binding.run(_source_locals)

    @_theme_source_wraps(_theme_original_class.__deepcopy__)
    def __deepcopy__(self, memo=None):
        _source_locals = _theme_state_binding.locals(1, self, memo)
        del self, memo
        return _theme_state_binding.run(_source_locals)

    @_theme_source_wraps(_theme_original_class.model_copy)
    def model_copy(self, *, update=None, deep=False):
        _source_locals = _theme_state_binding.locals(2, self, update, deep)
        del self, update, deep
        return _theme_state_binding.run(_source_locals)

    @_theme_source_wraps(_theme_original_class.__getstate__)
    def __getstate__(self):
        _source_locals = _theme_state_binding.locals(3, self)
        del self
        return _theme_state_binding.run(_source_locals)

    @_theme_source_wraps(_theme_original_class.__setstate__)
    def __setstate__(self, state):
        _source_locals = _theme_state_binding.locals(4, self, state)
        del self, state
        return _theme_state_binding.run(_source_locals)


_theme_state_binding.bind_assignment(
    Theme, _theme_assignment_base_metadata(_theme_state_binding),
)

_theme_state_binding.bind_selector(
    Theme, _theme_selector_base_metadata(_theme_state_binding),
    _theme_selector_private_descriptor_metadata,
    _theme_selector_property_descriptor_metadata,
)

_theme_state_binding.bind_validate_assignment(_theme_validate_assignment_metadata)
_theme_state_binding.bind_simple_assignment_handlers(_theme_simple_assignment_metadata)
_theme_state_binding.bind_selector_owned_policies(_theme_selector_policy_metadata)


_native.theme_regex_initialize()


def _platform_aware_font_size_adjustment(pt: str) -> float:
    """Rescale font size adjustments to 72 dpi (macOS)

    Account for platform DPI differences in font size adjustments.
    macOS uses 72 dpi while windows and linux use 96, so in order for
    increases and decreases in font size remain proportional,
    they also need to be scaled by a factor of 96/72.
    """
    _source_locals = (...).__call__(0, ('darwin', 96, 72), pt)
    del pt
    return (...).__call__(_source_locals)


def decrease(font_size: str, pt: str) -> str:
    """Decrease fontsize."""
    _source_locals = (...).__call__(1, (-2, 'pt'), font_size, pt)
    del font_size, pt
    return (...).__call__(_source_locals)


def increase(font_size: str, pt: str) -> str:
    """Increase fontsize."""
    _source_locals = (...).__call__(2, (-2, 'pt'), font_size, pt)
    del font_size, pt
    return (...).__call__(_source_locals)


def _parse_color_as_rgb(color: str | Color) -> tuple[int, int, int]:
    _source_locals = (...).__call__(
        3, ('rgb(', ')', 3), color,
    )
    del color
    return (...).__call__(_source_locals)


def _shift_luminance(
    color: tuple[float, float, float], percentage: float
) -> tuple[int, int, int]:
    """Darkens or lightens a color preserving hue and perceived saturation."""
    _source_locals = (...).__call__(
        4, (100, 0.299, 0.587, 0.114, 0, 255.0, 1.0, 0.0, 255, 0.5, 3.0), color, percentage,
    )
    del color, percentage
    return (...).__call__(_source_locals)










































def darken(
    color: str | Color,
    percentage: float = 10,
    theme_type: Literal['dark', 'light'] = 'dark',
) -> str:
    _source_locals = (...).__call__(5, ('dark', -1, 1, 'rgb(', ', ', ')'), color, percentage, theme_type)
    del color, percentage, theme_type
    return (...).__call__(_source_locals)



def lighten(
    color: str | Color,
    percentage: float = 10,
    theme_type: Literal['dark', 'light'] = 'dark',
) -> str:
    _source_locals = (...).__call__(6, ('light', -1, 1, 'rgb(', ', ', ')'), color, percentage, theme_type)
    del color, percentage, theme_type
    return (...).__call__(_source_locals)



def opacity(color: str | Color, value: int = 255) -> str:
    _source_locals = (...).__call__(7, ('rgba(', ', ', ')', 255, 0), color, value)
    del color, value
    return (...).__call__(_source_locals)


def gradient(stops, horizontal: bool = True) -> str:

    _source_locals = (...).__call__(
        8, ('qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0, ', 'qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, ', 'stop: ', ' ', ', ', ')'), stops, horizontal,
    )
    del stops, horizontal
    return (...).__call__(_source_locals)






def template(css: str, **theme):
    _source_locals = (...).__call__(
        9, ('type', 'h', '-', '{{ ', ' }}'), css, theme,
    )
    del css, theme
    return (...).__call__(_source_locals)





































# Template callback declaration envelopes only; C++ owns all six bodies.
def _theme_template_callback_metadata(context):
    if context.entry == 15:
        def callback(matchobj):
            _source_locals = (...).__call__(context, matchobj)
            del matchobj
            return (...).__call__(_source_locals)
    else:
        theme = None

        def callback(matchobj):
            nonlocal theme
            _source_locals = (...).__call__(context, matchobj)
            del matchobj
            return (...).__call__(_source_locals)

    return callback


_theme_template_binding = _native.theme_template_bind(
    (_platform_aware_font_size_adjustment, decrease, increase, _parse_color_as_rgb,
     _shift_luminance, darken, lighten, opacity, gradient, template),
    _theme_template_callback_metadata,
)


def get_system_theme() -> str:
    """Return the system default theme, either 'dark', or 'light'.

    Note: uses Qt6 (version >6.5) property colorScheme
    """
    locals_owner = _native.theme_color_locals(3)
    return _native.theme_color_run(
        3, globals(), locals_owner,
        (('qtpy', ('QT6',), 0), ('qtpy.QtCore', ('Qt',), 0), ('qtpy.QtGui', ('QGuiApplication',), 0), 'dark', 'System theme detection requires a Qt6 backend. Please switch to PyQt6 or PySide6 to use it.', 2, ('stacklevel',), 'light'),
    )


def get_theme(theme_id: str):
    """Get a copy of theme based on its id.

    If you get a copy of the theme, changes to the theme model will not be
    reflected in the UI unless you replace or add the modified theme to
    the `_themes` container.

    Parameters
    ----------
    theme_id : str
        ID of requested theme.

    Returns
    -------
    theme: dict of str: str
        Theme mapping elements to colors. A copy is created
        so that manipulating this theme can be done without
        side effects.
    """
    locals_owner = _native.theme_color_locals(4, theme_id)
    del theme_id
    return _native.theme_color_run(
        4, globals(), locals_owner,
        ('system', 'Unrecognized theme ', '. Available themes are '),
    )


_themes: EventedDict[str, Theme] = EventedDict(basetype=Theme)


def register_theme(theme_id, theme, source):
    """Register a new or updated theme.

    Parameters
    ----------
    theme_id : str
        id of requested theme.
    theme : dict of str: str, Theme
        Theme mapping elements to colors.
    source : str
        Source plugin of theme
    """
    locals_owner = _native.theme_registration_locals(3, theme_id, theme, source)
    del theme_id, theme, source
    return _native.theme_registration_run(
        3, globals(), locals_owner, (__debug__,),
    )


def unregister_theme(theme_id):
    """Remove existing theme.

    Parameters
    ----------
    theme_id : str
        id of the theme to be removed.
    """
    locals_owner = _native.theme_registration_locals(4, theme_id)
    del theme_id
    return _native.theme_registration_run(
        4, globals(), locals_owner, (),
    )


def available_themes() -> list[str]:
    """List available themes.

    Returns
    -------
    list of str
        ids of available themes.
    """
    locals_owner = _native.theme_color_locals(5)
    return _native.theme_color_run(
        5, globals(), locals_owner,
        ('system',),
    )


def is_theme_available(theme_id):
    """Check if a theme is available.

    Parameters
    ----------
    theme_id : str
        id of requested theme.

    Returns
    -------
    bool
        True if the theme is available, False otherwise.
    """
    locals_owner = _native.theme_registration_locals(5, theme_id)
    del theme_id
    return _native.theme_registration_run(
        5, globals(), locals_owner, ('system',),
    )


def rebuild_theme_settings():
    """update theme information in settings.

    here we simply update the settings to reflect current list of available
    themes.
    """
    locals_owner = _native.theme_registration_locals(6)
    return _native.theme_registration_run(
        6, globals(), locals_owner, (('napari.settings', ('get_settings',), 0),),
    )


# Note: these colors are sometimes lightened / darkened in the qss file.
DARK = Theme(
    id='dark',
    type='dark',
    label='Default Dark',
    background='rgb(35, 36, 43)',
    foreground='rgb(46, 51, 62)',
    primary='rgb(66, 74, 84)',
    secondary='rgb(86, 95, 108)',
    highlight='rgb(97, 105, 110)',
    text='rgb(240, 241, 242)',
    icon='rgb(209, 210, 212)',
    warning='rgb(227, 182, 23)',
    error='rgb(153, 18, 31)',
    current='rgb(69, 96, 196)',
    syntax_style='native',
    console='rgb(18, 18, 18)',
    canvas='black',
    font_size='12pt' if sys.platform == 'darwin' else '9pt',
)
LIGHT = Theme(
    id='light',
    type='light',
    label='Default Light',
    background='rgb(235, 231, 230)',
    foreground='rgb(221, 218, 216)',
    primary='rgb(197, 195, 193)',
    secondary='rgb(180, 178, 175)',
    highlight='rgb(175, 172, 170)',
    text='rgb(30, 30, 33)',
    icon='rgb(62, 63, 65)',
    warning='rgb(227, 182, 23)',
    error='rgb(255, 18, 31)',
    current='rgb(160, 184, 255)',
    syntax_style='default',
    console='white',
    canvas='white',
    font_size='12pt' if sys.platform == 'darwin' else '9pt',
)

register_theme('dark', DARK, 'builtin')
register_theme('light', LIGHT, 'builtin')


# this function here instead of plugins._npe2 to avoid circular import
def _install_npe2_themes(themes=None):
    locals_owner = _native.theme_registration_locals(7, themes)
    del themes
    return _native.theme_registration_run(
        7, globals(), locals_owner, ('npe2', 0, False, ('disabled',), (), 'colors', True, ('exclude', 'exclude_unset'), ('exclude_unset',), 'napari', 'Registration theme failed.'),
    )


_install_npe2_themes(_themes)
_themes.events.added.connect(rebuild_theme_settings)
_themes.events.removed.connect(rebuild_theme_settings)
