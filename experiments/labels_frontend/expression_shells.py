"""Inert lazy-generator transports with real, source-named closure cells."""


def e0(_iterator):
    _state = (...).__call__(-2, _iterator, None)
    try:
        _value = _state.send(None)
        while True:
            try:
                _request = yield _value
            except 0 as _error:
                _value = _state.throw(_error)
            else:
                _value = _state.send(_request)
    except 1:
        return


def distance_factory(center, grids, normalized_scale):
    def e1(_iterator):
        _state = (...).__call__(-2, _iterator, lambda: (center, grids, normalized_scale))
        try:
            _value = _state.send(None)
            while True:
                try:
                    _request = yield _value
                except 0 as _error:
                    _value = _state.throw(_error)
                else:
                    _value = _state.send(_request)
        except 1:
            return
    return e1


def axes_factory(i):
    def e2(_iterator):
        _state = (...).__call__(-2, _iterator, lambda: i)
        try:
            _value = _state.send(None)
            while True:
                try:
                    _request = yield _value
                except 0 as _error:
                    _value = _state.throw(_error)
                else:
                    _value = _state.send(_request)
        except 1:
            return
    return e2


def modifiers_factory(event):
    def e3(_iterator):
        _state = (...).__call__(-2, _iterator, lambda: event)
        try:
            _value = _state.send(None)
            while True:
                try:
                    _request = yield _value
                except 0 as _error:
                    _value = _state.throw(_error)
                else:
                    _value = _state.send(_request)
        except 1:
            return
    return e3


def seed_factory(int_coord):
    def e4(_iterator):
        _state = (...).__call__(-2, _iterator, lambda: int_coord)
        try:
            _value = _state.send(None)
            while True:
                try:
                    _request = yield _value
                except 0 as _error:
                    _value = _state.throw(_error)
                else:
                    _value = _state.send(_request)
        except 1:
            return
    return e4


def e5(_iterator):
    _state = (...).__call__(-2, _iterator, None)
    try:
        _value = _state.send(None)
        while True:
            try:
                _request = yield _value
            except 0 as _error:
                _value = _state.throw(_error)
            else:
                _value = _state.send(_request)
    except 1:
        return


def e6(_iterator):
    _state = (...).__call__(-2, _iterator, None)
    try:
        _value = _state.send(None)
        while True:
            try:
                _request = yield _value
            except 0 as _error:
                _value = _state.throw(_error)
            else:
                _value = _state.send(_request)
    except 1:
        return
