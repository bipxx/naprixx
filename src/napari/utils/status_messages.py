from naparixx import _native
from collections.abc import Iterable

import numpy as np
import numpy.typing as npt

DEFAULT_PRECISION = 3


def format_float(value, precision=None, format_mode='g'):
    """Nice float formatting into strings."""
    from naparixx import _native

    locals_owner = _native.layer_format_locals(value, precision, format_mode)
    del value, precision, format_mode
    return _native.layer_format_float(globals(), locals_owner)


def format_feature_value(value):
    """Format a single feature value for display in the status bar."""
    return _native.viewer_status_format_feature(value, globals())


def status_format(value, precision=None, format_mode='g'):
    """Return a "nice" string representation of a value.

    Parameters
    ----------
    value : Any
        The value to be printed.

    Returns
    -------
    formatted : str
        The string resulting from formatting.

    Examples
    --------
    >>> values = np.array([1, 10, 100, 1000, 1e6, 6.283, 123.932021,
    ...                    1123.9392001, 2 * np.pi, np.exp(1)])
    >>> status_format(values)
    '[1, 10, 100, 1e+03, 1e+06, 6.28, 124, 1.12e+03, 6.28, 2.72]'
    """
    owner = _native.viewer_status_format_locals(value, precision, format_mode)
    del value, precision, format_mode
    return _native.viewer_status_format_value(globals(), ("[", ", ", "]", ""), owner)


def generate_layer_status_strings(
    position: npt.ArrayLike | None,
    value: tuple | None,
    precision: int | None = None,
) -> tuple[str, str]:
    owner = _native.viewer_status_strings_locals(position, value, precision)
    del position, value, precision
    return _native.viewer_status_generate_strings(globals(), ("d", "f", "", (None, None), ", "), owner)


def generate_layer_coords_status(
    position: npt.ArrayLike | None, value: tuple | None
) -> str:
    return ': '.join(generate_layer_status_strings(position, value))


def generate_layer_status(name, position, value):
    """Generate a status message based on the coordinates and value

    Parameters
    ----------
    name : str
        Name of the layer.
    position : tuple or list
        List of coordinates, say of the cursor.
    value : Any
        The value to be printed.

    Returns
    -------
    msg : string
        String containing a message that can be used as a status update.
    """
    if position is not None:
        full_coord = map(str, np.round(position).astype(int))
        msg = f'{name} [{" ".join(full_coord)}]'
    else:
        msg = f'{name}'

    if value is not None:
        if isinstance(value, tuple) and value != (None, None):
            # it's a multiscale -> value = (data_level, value)
            msg += f': {status_format(value[0])}'
            if value[1] is not None:
                msg += f', {status_format(value[1])}'
        else:
            # it's either a grayscale or rgb image (scalar or list)
            msg += f': {status_format(value)}'
    return msg
