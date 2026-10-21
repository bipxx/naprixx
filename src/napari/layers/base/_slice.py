from itertools import count

from naparixx import native as _native

# We use an incrementing non-negative integer to uniquely identify
# slices that is unbounded based on Python 3's int.
_request_ids = count()


def _next_request_id() -> int:
    """Returns the next integer identifier associated with a slice."""
    return _native.slice_dataclass_next_request_id(globals())
