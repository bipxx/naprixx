"""Ownership and provider boundaries for the registered Qt focus callback."""

import gc
import weakref

import pytest

from naparixx import _native


class Callback:
    def __call__(self, *args):
        pass


class Connection:
    pass


class Signal:
    def __init__(self):
        self.callbacks = []
        self.calls = []
        self.connection_factory = Connection
        self.connect_error = None
        self.disconnect_error = None
        self.on_disconnect = None

    def connect(self, callback):
        self.calls.append(("connect", callback))
        if self.connect_error is not None:
            raise self.connect_error
        self.callbacks.append(callback)
        return self.connection_factory()

    def disconnect(self, callback):
        self.calls.append(("disconnect", callback))
        if self.on_disconnect is not None:
            self.on_disconnect()
        if self.disconnect_error is not None:
            raise self.disconnect_error
        for index, existing in enumerate(self.callbacks):
            if existing is callback:
                self.callbacks.pop(index)
                return
        raise TypeError("callback is already disconnected")


class Application:
    def __init__(self):
        self.focusChanged = Signal()


def test_record_keeps_exact_callback_and_connection_with_read_only_views():
    app = Application()
    foreign, callback = Callback(), Callback()
    app.focusChanged.connect(foreign)
    assert _native.qt_connect_focus_callback(app, callback) is None
    record = app._naparixx_focus_connection
    connection = record.connection
    assert record.callback is callback
    assert isinstance(connection, Connection)
    assert gc.is_tracked(record)
    assert not hasattr(record, "__dict__")
    for name in ("callback", "connection", "extra"):
        with pytest.raises(AttributeError):
            setattr(record, name, None)
    assert _native.qt_release_focus_connection(app)
    assert app.focusChanged.callbacks == [foreign]
    assert app.focusChanged.calls[-1] == ("disconnect", callback)
    assert not hasattr(app, "_naparixx_focus_connection")
    assert record.callback is None and record.connection is None
    assert not _native.qt_release_focus_connection(app)


def test_reconnecting_identical_callback_is_a_no_op():
    app, callback = Application(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    _native.qt_connect_focus_callback(app, callback)
    assert app._naparixx_focus_connection is record
    assert app.focusChanged.calls == [("connect", callback)]
    _native.qt_release_focus_connection(app)


def test_release_drops_callback_and_connection_while_record_remains_alive():
    app, callback = Application(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    callback_reference = weakref.ref(callback)
    connection_reference = weakref.ref(record.connection)
    app.focusChanged.calls.clear()
    del callback
    assert callback_reference() is record.callback
    assert connection_reference() is record.connection
    assert _native.qt_release_focus_connection(app)
    app.focusChanged.calls.clear()
    assert callback_reference() is None and connection_reference() is None
    assert record.callback is None and record.connection is None


def test_replacement_does_not_disconnect_the_registered_callback():
    app, callback, replacement = Application(), Callback(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    with pytest.raises(RuntimeError, match="already connected"):
        _native.qt_connect_focus_callback(app, replacement)
    assert app._naparixx_focus_connection is record
    assert record.callback is callback
    assert app.focusChanged.calls == [("connect", callback)]
    _native.qt_release_focus_connection(app)


def test_foreign_record_is_preserved():
    app, foreign = Application(), object()
    app._naparixx_focus_connection = foreign
    assert not _native.qt_release_focus_connection(app)
    with pytest.raises(RuntimeError, match="occupied"):
        _native.qt_connect_focus_callback(app, Callback())
    assert app._naparixx_focus_connection is foreign
    assert not app.focusChanged.calls


@pytest.mark.parametrize("failure", [TypeError, RuntimeError])
def test_already_destroyed_connection_still_releases_its_record(failure):
    app, callback = Application(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    app.focusChanged.disconnect_error = failure("already gone")
    assert _native.qt_release_focus_connection(app)
    assert not hasattr(app, "_naparixx_focus_connection")
    assert record.callback is None and record.connection is None
    assert not _native.qt_release_focus_connection(app)


@pytest.mark.parametrize("failure", [ValueError, AttributeError])
def test_unexpected_disconnect_error_preserves_identity_and_owned_state(failure):
    app, callback = Application(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    connection = record.connection
    error = failure("provider disconnect failed")
    app.focusChanged.disconnect_error = error
    with pytest.raises(failure) as caught:
        _native.qt_release_focus_connection(app)
    assert caught.value is error
    assert app._naparixx_focus_connection is record
    assert record.callback is callback and record.connection is connection
    app.focusChanged.disconnect_error = None
    assert _native.qt_release_focus_connection(app)


def test_connect_error_preserves_identity_without_publishing_a_record():
    app, callback = Application(), Callback()
    error = ValueError("provider connect failed")
    app.focusChanged.connect_error = error
    with pytest.raises(ValueError) as caught:
        _native.qt_connect_focus_callback(app, callback)
    assert caught.value is error
    assert not hasattr(app, "_naparixx_focus_connection")
    assert app.focusChanged.calls == [("connect", callback)]


@pytest.mark.parametrize("publish_before_error", [False, True])
def test_publication_error_rolls_back_only_the_new_callback(publish_before_error):
    error = ValueError("provider publication failed")

    class RejectPublish(Application):
        def __setattr__(self, name, value):
            if name == "_naparixx_focus_connection":
                if publish_before_error:
                    object.__setattr__(self, name, value)
                raise error
            object.__setattr__(self, name, value)

    app, foreign, callback = RejectPublish(), Callback(), Callback()
    app.focusChanged.connect(foreign)
    connection_references = []

    def make_connection():
        connection = Connection()
        connection_references.append(weakref.ref(connection))
        return connection

    app.focusChanged.connection_factory = make_connection
    with pytest.raises(ValueError) as caught:
        _native.qt_connect_focus_callback(app, callback)
    assert caught.value is error
    assert app.focusChanged.callbacks == [foreign]
    assert app.focusChanged.calls[-2:] == [
        ("connect", callback),
        ("disconnect", callback),
    ]
    assert not hasattr(app, "_naparixx_focus_connection")
    assert connection_references[0]() is None


def test_publication_error_survives_a_rollback_disconnect_error():
    error = ValueError("publication failed")

    class RejectPublish(Application):
        def __setattr__(self, name, value):
            if name == "_naparixx_focus_connection":
                raise error
            object.__setattr__(self, name, value)

    app, callback = RejectPublish(), Callback()
    app.focusChanged.disconnect_error = LookupError("rollback failed")
    with pytest.raises(ValueError) as caught:
        _native.qt_connect_focus_callback(app, callback)
    assert caught.value is error
    assert app.focusChanged.calls[-1] == ("disconnect", callback)
    assert not hasattr(app, "_naparixx_focus_connection")


def test_disconnect_reentry_does_not_repeat_provider_calls():
    app, callback = Application(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    nested_results = []
    app.focusChanged.on_disconnect = lambda: nested_results.append(
        _native.qt_release_focus_connection(app)
    )
    assert _native.qt_release_focus_connection(app)
    assert nested_results == [False]
    assert app.focusChanged.calls == [
        ("connect", callback),
        ("disconnect", callback),
    ]


def test_payload_finalizer_reentry_sees_both_edges_detached():
    app, callback = Application(), Callback()
    observations = []
    app_reference = weakref.ref(app)

    class FinalizingConnection:
        record_reference = None

        def __del__(self):
            application = app_reference()
            record = self.record_reference()
            observations.append(
                (
                    record.callback,
                    record.connection,
                    hasattr(application, "_naparixx_focus_connection"),
                    _native.qt_release_focus_connection(application),
                )
            )

    app.focusChanged.connection_factory = FinalizingConnection
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    record.connection.record_reference = weakref.ref(record)
    assert _native.qt_release_focus_connection(app)
    assert observations == [(None, None, False, False)]


@pytest.mark.parametrize("edge", ["callback", "connection"])
def test_cycles_through_either_record_edge_are_collectible(edge):
    app, callback = Application(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    connection = record.connection
    if edge == "callback":
        callback.application = app
    else:
        connection.application = app
    references = [weakref.ref(value) for value in (app, callback, record, connection)]
    del app, callback, record, connection
    gc.collect()
    assert all(reference() is None for reference in references)


def test_record_does_not_retain_the_application():
    app, callback = Application(), Callback()
    _native.qt_connect_focus_callback(app, callback)
    record = app._naparixx_focus_connection
    app_reference = weakref.ref(app)
    callback_reference = weakref.ref(callback)
    connection_reference = weakref.ref(record.connection)
    del app, callback
    assert app_reference() is None
    assert callback_reference() is record.callback
    assert connection_reference() is record.connection
    del record
    assert callback_reference() is None and connection_reference() is None


def test_uninitialized_record_can_be_traversed_and_released_safely():
    record_type = _native.QtFocusConnection
    record = record_type.__new__(record_type)
    app = Application()
    app._naparixx_focus_connection = record
    assert not _native.qt_release_focus_connection(app)
    reference = weakref.ref(record)
    del app, record
    gc.collect()
    assert reference() is None


def test_release_without_an_application_has_no_record():
    assert not _native.qt_release_focus_connection(None)
