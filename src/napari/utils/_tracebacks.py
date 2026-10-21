from __future__ import annotations

from naparixx import _native

import re
from typing import TYPE_CHECKING

import numpy as np

from napari.types import ExcInfo

if TYPE_CHECKING:
    from collections.abc import Callable, Generator


def get_tb_formatter() -> Callable[[ExcInfo, bool, str], str]:
    """Return a formatter callable that uses IPython VerboseTB if available.

    Imports IPython lazily if available to take advantage of ultratb.VerboseTB.
    If unavailable, cgitb is used instead, but this function overrides a lot of
    the hardcoded citgb styles and adds error chaining (for exceptions that
    result from other exceptions).

    Returns
    -------
    callable
        A function that accepts a 3-tuple and a boolean ``(exc_info, as_html)``
        and returns a formatted traceback string. The ``exc_info`` tuple is of
        the ``(type, value, traceback)`` format returned by sys.exc_info().
        The ``as_html`` determines whether the traceback is formatted in html
        or plain text.
    """
    locals_owner = _native.traceback_locals()
    def _ipython_metadata(IPython):
        def format_exc_info(info: ExcInfo, as_html: bool, color: str = 'Neutral') -> str:
            locals_owner = _native.traceback_locals()
            return _native.traceback_ipython(info, as_html, color, globals(), lambda: IPython, ((9, 0), ' ', '&nbsp;', '', '\n', '<br>', "<span style='font-family: monaco,courier,monospace;'>", '</span>'), locals_owner)
        return format_exc_info
    def _fallback_metadata(traceback):
        def format_exc_info(info: ExcInfo, as_html: bool, color: str = 'Neutral') -> str:
            locals_owner = _native.traceback_locals()
            return _native.traceback_fallback(info, as_html, color, globals(), lambda: traceback, ('', '<pre>', '</pre>'), locals_owner)
        return format_exc_info
    return _native.traceback_factory(globals(), _ipython_metadata, _fallback_metadata, ('IPython', 'IPython.core.ultratb', 'traceback'), locals_owner)


ANSI_STYLES = {
    1: {'font_weight': 'bold'},
    2: {'font_weight': 'lighter'},
    3: {'font_weight': 'italic'},
    4: {'text_decoration': 'underline'},
    5: {'text_decoration': 'blink'},
    6: {'text_decoration': 'blink'},
    8: {'visibility': 'hidden'},
    9: {'text_decoration': 'line-through'},
    30: {'color': 'black'},
    31: {'color': 'red'},
    32: {'color': 'green'},
    33: {'color': 'yellow'},
    34: {'color': 'blue'},
    35: {'color': 'magenta'},
    36: {'color': 'cyan'},
    37: {'color': 'white'},
}


def ansi2html(
    ansi_string: str, styles: dict[int, dict[str, str]] = ANSI_STYLES
) -> Generator[str, None, None]:
    """Convert ansi string to colored HTML

    Parameters
    ----------
    ansi_string : str
        text with ANSI color codes.
    styles : dict, optional
        A mapping from ANSI codes to a dict of css kwargs:values,
        by default ANSI_STYLES

    Yields
    ------
    str
        HTML strings that can be joined to form the final html
    """
    locals_owner = _native.traceback_ansi_iterator(ansi_string, styles, globals(), ('\x1b\\[([\\d;]*)([a-zA-Z])', 'mM', ';', '; ', ': ', '</span>', '<span style="{}">'))
    del ansi_string, styles
    yield from locals_owner
