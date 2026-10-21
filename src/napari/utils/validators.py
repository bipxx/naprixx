from collections.abc import Collection, Generator, Iterable
from itertools import tee

from naparixx import _native


def validate_n_seq(n: int, dtype=None):
    """Creates a function to validate a sequence of len == N and type == dtype.

    Currently does **not** validate generators (will always validate true).

    Parameters
    ----------
    n : int
        Desired length of the sequence
    dtype : type, optional
        If provided each item in the sequence must match dtype, by default None

    Returns
    -------
    function
        Function that can be called on an object to validate that is a sequence
        of len `n` and (optionally) each item in the sequence has type `dtype`

    Examples
    --------
    >>> validate = validate_N_seq(2)
    >>> validate(8)  # raises TypeError
    >>> validate([1, 2, 3])  # raises ValueError
    >>> validate([4, 5])  # just fine, thank you very much
    """

    def func(obj):
        """Function that validates whether an object is a sequence of len `n`.

        Parameters
        ----------
        obj : any
            the object to be validated

        Raises
        ------
        TypeError
            If the object is not an indexable collection.
        ValueError
            If the object does not have length `n`
        TypeError
            If `dtype` was provided to the wrapper function and all items in
            the sequence are not of type `dtype`.
        """

        locals_owner = _native.validator_sequence_locals()
        return _native.validator_sequence_validate(
            obj, n, dtype, globals(), locals_owner
        )

    return _native.validator_n_seq_bind(func)


def _pairwise(iterable: Iterable):
    """Convert iterable to a zip object containing tuples of pairs along the
    sequence.

    Examples
    --------
    >>> pairwise([1, 2, 3, 4])
    <zip at 0x10606df80>

    >>> list(pairwise([1, 2, 3, 4]))
    [(1, 2), (2, 3), (3, 4)]
    """
    locals_owner = _native.validator_pairwise_locals()
    return _native.validator_pairwise(iterable, globals(), locals_owner)


def _validate_increasing(values: Iterable) -> None:
    """Ensure that values in an iterable are monotocially increasing.

    Examples
    --------
    >>> _validate_increasing([1, 2, 3, 4])
    None

    >>> _validate_increasing([1, 4, 3, 4])
    ValueError: Sequence [1, 4, 3, 4] must be monotonically increasing.

    Raises
    ------
    ValueError
        If `values` is constant or decreasing from one value to the next.
    """
    _native.validator_increasing(values, globals())
