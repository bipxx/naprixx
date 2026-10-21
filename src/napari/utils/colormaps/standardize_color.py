"""This module contains functions that 'standardize' the color handling
of napari layers by supplying functions that are able to convert most
color representation the user had in mind into a single representation -
a numpy Nx4 array of float32 values between 0 and 1 - that is used across
the codebase. The color is always in an RGBA format. To handle colors in
HSV, for example, we should point users to skimage, matplotlib and others.

The main function of the module is "transform_color", which might call
a cascade of other, private, function in the module to do the hard work
of converting the input. This function will either be called directly, or
used by the function "transform_color_with_defaults", which is a helper
function for the layer objects located in
``layers.utils.color_transformations.py``.

In general, when handling colors we try to catch invalid color
representations, warn the users of their misbehaving and return a default
white color array, since it seems unreasonable to crash the entire napari
session due to mis-represented colors.
"""

from __future__ import annotations

from naparixx import _native

import functools
import types
import warnings
from typing import TYPE_CHECKING, Any

import numpy as np
from vispy.color import ColorArray, get_color_dict, get_color_names
from vispy.color.color_array import _string_to_rgb

if TYPE_CHECKING:
    from collections.abc import Callable, Sequence


def transform_color(colors: Any) -> np.ndarray:
    """Transforms provided color(s) to an Nx4 array of RGBA np.float32
    values.

    N is the number of given colors. The function is designed to parse all
    valid color representations a user might have and convert them properly.
    That being said, combinations of different color representation in the
    same list of colors is prohibited, and will error. This means that a list
    of ['red', np.array([1, 0, 0])] cannot be parsed and has to be manually
    pre-processed by the user before sent to this function. In addition, the
    provided colors - if numeric - should already be in an RGB(A) format. To
    convert an existing numeric color array to RGBA format use skimage before
    calling this function.

    Parameters
    ----------
    colors : string and array-like.
        The color(s) to interpret and convert

    Returns
    -------
    colors : np.ndarray
        An instance of np.ndarray with a data type of float32, 4 columns in
        RGBA order and N rows, with N being the number of colors. The array
        will always be 2D even if a single color is passed.

    Raises
    ------
    ValueError, AttributeError, KeyError
        invalid inputs
    """
    locals_owner = _native.theme_color_locals(8, colors)
    del colors
    return _native.theme_color_run(
        8, globals(), locals_owner,
        ("cannot convert type '", "' to a color array."),
    )


@functools.lru_cache(maxsize=1024)
def _handle_str(color: str) -> np.ndarray:
    """Creates an array from a color of type string.

    The function uses an LRU cache to enhance performance.

    Parameters
    ----------
    color : str
        A single string as an input color. Can be a color name or a
        hex representation of a color, with either 6 or 8 hex digits.

    Returns
    -------
    colorarray : np.ndarray
        1x4 array

    """
    locals_owner = _native.theme_color_locals(9, color)
    del color
    return _native.theme_color_run(
        9, globals(), locals_owner,
        (0, 'Empty string detected. Returning black instead.', (1, 4), ('dtype',), 3, 1.0),
    )


def _handle_list_like(colors: Sequence) -> np.ndarray | None:
    """Parse a list-like container of colors into a numpy Nx4 array.

    Handles all list-like containers of colors using recursion (if necessary).
    The colors inside the container should all be represented in the same
    manner. This means that a list containing ['r', (1., 1., 1.)] will raise
    an error. Note that numpy arrays are handled in _handle_array. Lists which
    are known to contain strings will be parsed with _handle_str_list_like.
    Generators should first visit _handle_generator before arriving as input.

    Parameters
    ----------
    colors : Sequence
        A list-like container of colors. The colors inside should be homogeneous
        in their representation.

    Returns
    -------
    color_array : np.ndarray
        Nx4 numpy array, with N being the length of ``colors``.
    """
    locals_owner = _native.theme_color_locals(10, colors)
    del colors
    return _native.theme_color_run(
        10, globals(), locals_owner,
        (0, 'O', ('dtype',), "Couldn't convert input color array to a proper numpy array. Please make sure that your input data is in a parsable format. Converting input to a white color array.", 1, ('dtype',), ('f', 'i'), ('U', 'O')),
    )


def _handle_generator(colors) -> np.ndarray | None:
    """Generators are converted to lists since we need to know their
    length to instantiate a proper array.
    """
    locals_owner = _native.theme_color_locals(11, colors)
    del colors
    return _native.theme_color_run(
        11, globals(), locals_owner,
        (),
    )


def handle_nested_colors(colors) -> ColorArray:
    """In case of an array-like container holding colors, unpack it."""
    colors_as_rbga = np.ones((len(colors), 4), dtype=np.float32)
    for idx, color in enumerate(colors):
        colors_as_rbga[idx] = _color_switch[type(color)](color)
    return ColorArray(colors_as_rbga)


def _handle_array(colors: np.ndarray) -> np.ndarray:
    """Converts the given array into an array in the right format."""
    locals_owner = _native.theme_color_locals(12, colors)
    del colors
    return _native.theme_color_run(
        12, globals(), locals_owner,
        ('O', 'An object array was passed as the color input. Please convert its datatype before sending it to napari. Converting input to a white color array.', 1, ('dtype',), 'U', 'String color arrays should be one-dimensional. Converting input to a white color array.', 0, 'Given color input is empty. Converting input to a white color array.', (1, 4), 2, 'Given colors input should contain one or two dimensions. Received array with ', '.', (lambda value: value in {3, 4}).__code__.co_consts[1], 'Given color array has an unsupported format. Received the following array:\n', '\nA proper color array should have 3-4 columns with a row per data entry.', 3, 4, 'Given colors input should contain three or four columns. Received array with ', ' columns. Converting input to a white color array.', ('f', 'i', 'u'), 'Data type of array (', ') not supported.',),
    )


def _convert_array_to_correct_format(colors: np.ndarray) -> np.ndarray:
    """Asserts shape, dtype and normalization of given color array.

    This function deals with arrays which are already 'well-behaved',
    i.e. have (almost) the correct number of columns and are able to represent
    colors correctly. It then it makes sure that the array indeed has exactly
    four columns and that its values are normalized between 0 and 1, with a
    data type of float32.

    Parameters
    ----------
    colors : np.ndarray
        Input color array, perhaps un-normalized and without the alpha channel.

    Returns
    -------
    colors : np.ndarray
        Nx4, float32 color array with values in the range [0, 1]
    """
    locals_owner = _native.theme_color_locals(13, colors)
    del colors
    return _native.theme_color_run(
        13, globals(), locals_owner,
        (3, ('dtype',), 0, 'Colors input had negative values.', 1, "Colors with values larger than one detected. napari will normalize these colors for you. If you'd like to convert these yourself, please use the proper method from skimage.color."),
    )


def _handle_str_list_like(colors: Sequence | np.ndarray) -> np.ndarray:
    """Converts lists or arrays filled with strings to the proper color array
    format.

    Parameters
    ----------
    colors : list-like
        A sequence of string colors

    Returns
    -------
    color_array : np.ndarray
        Nx4, float32 color array
    """
    locals_owner = _native.theme_color_locals(14, colors)
    del colors
    return _native.theme_color_run(
        14, globals(), locals_owner,
        (4, ('dtype',), 'Invalid color found: ', ' at index ', '.'),
    )


def _handle_none(color) -> np.ndarray:
    """Converts color given as None to black.

    Parameters
    ----------
    color : NoneType
        None value given as a color

    Returns
    -------
    arr : np.ndarray
        1x4 numpy array of float32 zeros

    """
    locals_owner = _native.theme_color_locals(15, color)
    del color
    return _native.theme_color_run(
        15, globals(), locals_owner,
        ((1, 4), ('dtype',)),
    )


def _normalize_color_array(colors: np.ndarray) -> np.ndarray:
    """Normalize all array values to the range [0, 1].

    The added complexity here stems from the fact that if a row in the given
    array contains four identical value a simple normalization might raise a
    division by zero exception.

    Parameters
    ----------
    colors : np.ndarray
        A numpy array with values possibly outside the range of [0, 1]

    Returns
    -------
    colors : np.ndarray
        Copy of input array with normalized values
    """
    locals_owner = _native.theme_color_locals(16, colors)
    del colors
    return _native.theme_color_run(
        16, globals(), locals_owner,
        (True, ('copy',), 1, 0, ('axis',)),
    )


_color_switch: dict[Any, Callable] = {
    str: _handle_str,
    np.str_: _handle_str,
    list: _handle_list_like,
    tuple: _handle_list_like,
    types.GeneratorType: _handle_generator,
    np.ndarray: _handle_array,
    type(None): _handle_none,
}


def _create_hex_to_name_dict():
    """Create a dictionary mapping hexadecimal RGB colors into their
    'official' name.

    Returns
    -------
    hex_to_rgb : dict
        Mapping from hexadecimal RGB ('#ff0000') to name ('red').
    """
    colordict = get_color_dict()
    hex_to_name = {f'{v.lower()}ff': k for k, v in colordict.items()}
    return hex_to_name


def get_color_namelist():
    """Gets all the color names supported by napari.

    Returns
    -------
    list[str]
        All the color names supported by napari.
    """
    return get_color_names()


hex_to_name = _create_hex_to_name_dict()


def _check_color_dim(val):
    """Ensures input is Nx4.

    Parameters
    ----------
    val : np.ndarray
        A color array of possibly less than 4 columns

    Returns
    -------
    val : np.ndarray
        A four columns version of the input array. If the original array
        was a missing the fourth channel, it's added as 1.0 values.
    """
    val = np.atleast_2d(val)
    if val.shape[1] not in (3, 4):
        strval = str(val)
        if len(strval) > 100:
            strval = strval[:97] + '...'
        raise RuntimeError(
            f'Value must have second dimension of size 3 or 4. Got `{strval}`, shape={val.shape}'
        )

    if val.shape[1] == 3:
        val = np.column_stack([val, np.float32(1.0)])
    return val


def rgb_to_hex(rgbs: Sequence) -> np.ndarray:
    """Convert RGB to hex quadruplet.

    Taken from vispy with slight modifications.

    Parameters
    ----------
    rgbs : Sequence
        A list-like container of colors in RGBA format with values
        between [0, 1]

    Returns
    -------
    arr : np.ndarray
        An array of the hex representation of the input colors

    """
    rgbs = _check_color_dim(rgbs)
    return np.array(
        [
            f'#{"%02x" * 4}' % tuple((255 * rgb).astype(np.uint8))
            for rgb in rgbs
        ],
        '|U9',
    )
