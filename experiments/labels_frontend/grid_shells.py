"""Inert declaration shells only; default provider policy is handwritten C++."""


def _grid_getitem(self, key):
    return (...).__call__(0, self, key)


def _grid_indices(dimensions, dtype=int, sparse=False):
    return (...).__call__(1)


def _grid_sparse_comp(_iterator):
    return (...).__call__(-2)
