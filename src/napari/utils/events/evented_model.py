import warnings
from collections.abc import Callable
from contextlib import contextmanager
from copy import copy
from typing import Any, ClassVar, Union

import numpy as np
from app_model.types import KeyBinding
from pydantic import (
    BaseModel,
    ConfigDict,
    PrivateAttr,
)
from pydantic._internal._model_construction import ModelMetaclass
from naparixx import _native

from napari._pydantic_util import get_inner_type, get_outer_type
from napari.utils.events.event import EmitterGroup, Event
from napari.utils.misc import pick_equality_operator

# encoders for non-napari specific field types.  To declare a custom encoder
# for a napari type, add a `_json_encode` method to the class itself.
# it will be added to the model json_encoders in :func:`EventedMetaclass.__new__`
_BASE_JSON_ENCODERS = {
    np.ndarray: lambda arr: arr.tolist(),
    KeyBinding: lambda v: str(v),
}


class EventedMetaclass(ModelMetaclass):
    """pydantic ModelMetaclass that preps "equality checking" operations.

    A metaclass is the thing that "constructs" a class, and ``ModelMetaclass``
    is where pydantic puts a lot of it's type introspection and ``ModelField``
    creation logic.  Here, we simply tack on one more function, that builds a
    ``cls.__eq_operators__`` dict which is mapping of field name to a function
    that can be called to check equality of the value of that field with some
    other object.  (used in ``EventedModel.__eq__``)

    This happens only once, when an ``EventedModel`` class is created (and not
    when each instance of an ``EventedModel`` is instantiated).
    """

    def __new__(mcs, name, bases, namespace, **kwargs):
        cls = super().__new__(mcs, name, bases, namespace, **kwargs)
        _native.model_prepare_class(cls, namespace)
        return cls


def _update_dependents_from_property_code(cls, prop_name, prop, deps, visited=()):
    """Recursively find all the dependents of a property by inspecting the code object.

    Update the given deps dictionary with the new findings.
    """
    _native.model_update_property_dependents(cls, prop_name, prop, deps, visited)


def _get_field_dependents(cls: "EventedModel") -> dict[str, set[str]]:
    """Return mapping of field name -> dependent set of property names.

    Dependencies will be guessed by inspecting the code of each property
    in order to emit an event for a computed property when a model field
    that it depends on changes (e.g: @property 'c' depends on model fields
    'a' and 'b'). Alternatvely, dependencies may be declared excplicitly
    in the Model Config.

    Note: accessing a field with `getattr()` instead of dot notation won't
    be automatically detected.

    Examples
    --------
        class MyModel(EventedModel):
            a: int = 1
            b: int = 1

            @property
            def c(self) -> List[int]:
                return [self.a, self.b]

            @c.setter
            def c(self, val: Sequence[int]):
                self.a, self.b = val

            @property
            def d(self) -> int:
                return sum(self.c)

            @d.setter
            def d(self, val: int):
                self.c = [val // 2, val // 2]

            model_config = ConfigDict(
                dependencies={
                    'c': ['a', 'b'],
                    'd': ['a', 'b']
                }
                )
    """
    return _native.model_field_dependents(cls)


class EventedModel(BaseModel, metaclass=EventedMetaclass):
    """A Model subclass that emits an event whenever a field value is changed.

    Note: As per the standard pydantic behavior, default Field values are
    not validated (#4138) and should be correctly typed.
    """

    # add private attributes for event emission
    _events: EmitterGroup = PrivateAttr(
        default_factory=lambda: EmitterGroup(_connect_children=False)
    )

    # mapping of name -> property obj for methods that are properties
    __properties__: ClassVar[dict[str, property]]
    __non_evented_properties__: ClassVar[set[str]] = {
        "events",
        "_defaults",
        "__pydantic_private__",
    }
    # mapping of field name -> dependent set of property names
    # when field is changed, an event for dependent properties will be emitted.
    __field_dependents__: ClassVar[dict[str, set[str]]]
    __eq_operators__: ClassVar[dict[str, Callable[[Any, Any], bool]]]
    _changes_queue: dict[str, Any] = PrivateAttr(default_factory=dict)
    _primary_changes: dict[str, None] = PrivateAttr(default_factory=dict)
    _delay_check_semaphore: int = PrivateAttr(0)
    _change_state: _native.ChangeJournal = PrivateAttr(
        default_factory=_native.ChangeJournal
    )
    _primary_view: _native.PrimaryChanges | None = PrivateAttr(None)
    __slots__: ClassVar[set[str]] = {
        "__weakref__",
        "_naparixx_transaction_owner",
    }

    # pydantic BaseModel configuration.  see:
    # https://pydantic-docs.helpmanual.io/usage/model_config/
    model_config = ConfigDict(
        # whether to allow arbitrary user types for fields (they are validated
        # simply by checking if the value is an instance of the type). If
        # False, RuntimeError will be raised on model declaration
        arbitrary_types_allowed=True,
        validate_assignment=True,
        validate_default=True,
        # https://pydantic-docs.helpmanual.io/usage/exporting_models/#modeljson
        # NOTE: json_encoders are also added EventedMetaclass.__new__ if the
        # field declares a _json_encode method.
        json_encoders=_BASE_JSON_ENCODERS,
    )

    def __init__(self, **kwargs) -> None:
        super().__init__(**kwargs)
        _native.model_initialize_events(self)

    @property
    def __pydantic_private__(self):
        return _native.model_private_get(self)

    @__pydantic_private__.setter
    def __pydantic_private__(self, value):
        _native.model_private_set(self, value)

    @__pydantic_private__.deleter
    def __pydantic_private__(self):
        _native.model_private_delete(self)

    def _super_setattr_(self, name: str, value: Any) -> None:
        _native.model_super_setattr(self, name, value)

    def _check_if_differ(self, name: str, old_value: Any) -> tuple[bool, Any]:
        """
        Check new value of a field and emit event if it is different from the old one.

        Returns True if data changed, else False. Return current value.
        """
        return _native.model_different(self, name, old_value)

    def __setattr__(self, name: str, value: Any) -> None:
        _native.model_setattr(self, name, value)

    def _check_if_values_changed_and_emit_if_needed(self):
        """
        Check if field values changed and emit events if needed.

        The advantage of moving this to the end of all the modifications is
        that comparisons will be performed only once for every potential change.
        """
        _native.model_emit_changes(self)

    def _setattr_impl(self, name: str, value: Any) -> None:
        _native.model_setattr_impl(self, name, value)

    def __copy__(self):
        return _native.model_shallow_copy(self, super())

    def __deepcopy__(self, memo=None):
        return _native.model_deep_copy(self, super(), memo)

    # expose the private EmitterGroup publicly
    @property
    def events(self) -> EmitterGroup:
        return self._events

    def _reset_event_source(self):
        """
        set the event sources of self and all the children to the correct values
        """
        _native.model_reset_event_source(self)

    @property
    def _defaults(self) -> dict[str, Any]:
        return get_defaults(self)

    def reset(self):
        """Reset the state of the model to default values."""
        _native.model_reset(self)

    def update(self, values: Union["EventedModel", dict], recurse: bool = True) -> None:
        """Update a model in place.

        Parameters
        ----------
        values : dict, napari.utils.events.EventedModel
            Values to update the model with. If an EventedModel is passed it is
            first converted to a dictionary. The keys of this dictionary must
            be found as attributes on the current model.
        recurse : bool
            If True, recursively update fields that are EventedModels.
            Otherwise, just update the immediate fields of this EventedModel,
            which is useful when the declared field type (e.g. ``Union``) can have
            different realized types with different fields.
        """
        _native.model_update(self, values, recurse)

    def __eq__(self, other) -> bool:
        """Check equality with another object.

        We override the pydantic approach (which just checks
        ``self.model_dump() == other.model_dump()``) to accommodate more complicated types
        like arrays, whose truth value is often ambiguous. ``__eq_operators__``
        is constructed in ``EqualityMetaclass.__new__``
        """
        return _native.model_equal(self, other)

    @contextmanager
    def enums_as_values(self, as_values: bool = True):
        """Temporarily override how enums are retrieved.

        Parameters
        ----------
        as_values : bool, optional
            Whether enums should be shown as values (or as enum objects),
            by default `True`
        """
        token = _native.model_enums_enter(self, as_values)
        try:
            yield
        finally:
            _native.model_enums_leave(token)

    @classmethod
    def model_json_schema(
        cls,
        *args,
        **kwargs,
    ) -> dict[str, Any]:
        """Generate a JSON schema for this model.

        This is required to prevent from mail formated docstring break docs build.
        """
        return super().model_json_schema(
            *args,
            **kwargs,
        )


def get_defaults(obj: BaseModel | type[BaseModel]) -> dict[str, Any]:
    """Get possibly nested default values for a Model object."""
    return _native.model_get_defaults(obj)


class ComparisonDelayer:
    def __init__(self, target: EventedModel):
        _native.model_delayer_initialize(self, target)

    def __enter__(self):
        _native.model_delay_enter(self._target)

    def __exit__(self, exc_type, exc_val, exc_tb):
        _native.model_delayer_exit(self)
