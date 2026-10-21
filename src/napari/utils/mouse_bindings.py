from collections.abc import Callable

from pydantic import BaseModel, Field, PrivateAttr


class MousemapProvider:
    """Mix-in to add mouse binding functionality.

    Attributes
    ----------
    mouse_move_callbacks : list
        Callbacks from when mouse moves with nothing pressed.
    mouse_drag_callbacks : list
        Callbacks from when mouse is pressed, dragged, and released.
    mouse_wheel_callbacks : list
        Callbacks from when mouse wheel is scrolled.
    mouse_double_click_callbacks : list
        Callbacks from when mouse wheel is scrolled.
    """

    mouse_move_callbacks: list[Callable]
    mouse_wheel_callbacks: list[Callable]
    mouse_drag_callbacks: list[Callable]
    mouse_double_click_callbacks: list[Callable]

    def __init__(self, *args, **kwargs) -> None:
        from naparixx import native as _native

        _native.layer_mousemap_construction_initialize(
            self, args, kwargs, lambda self: super().__init__
        )


class MousemapProviderPydantic(BaseModel):
    """Mix-in to add mouse binding functionality.

    Attributes
    ----------
    mouse_move_callbacks : list
        Callbacks from when mouse moves with nothing pressed.
    mouse_drag_callbacks : list
        Callbacks from when mouse is pressed, dragged, and released.
    mouse_wheel_callbacks : list
        Callbacks from when mouse wheel is scrolled.
    mouse_double_click_callbacks : list
        Callbacks from when mouse wheel is scrolled.
    """

    mouse_move_callbacks: list[Callable] = Field(default_factory=list)
    mouse_wheel_callbacks: list[Callable] = Field(default_factory=list)
    mouse_drag_callbacks: list[Callable] = Field(default_factory=list)
    mouse_double_click_callbacks: list[Callable] = Field(default_factory=list)
    _persisted_mouse_event: dict = PrivateAttr(default_factory=dict)
    _mouse_drag_gen: dict = PrivateAttr(default_factory=dict)
    _mouse_wheel_gen: dict = PrivateAttr(default_factory=dict)
