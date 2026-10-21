"""Inert state protocol template; exception markers require native wiring."""


def protocol(_state):
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
