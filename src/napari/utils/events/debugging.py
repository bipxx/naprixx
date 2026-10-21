import inspect
import os
import site
from textwrap import indent
from typing import TYPE_CHECKING

from pydantic import Field, PrivateAttr
from pydantic_settings import BaseSettings, SettingsConfigDict

from napari.utils.misc import ROOT_DIR
from naparixx import _native

try:
    from rich import print  # noqa: A004
except ModuleNotFoundError:
    print('TIP: run `pip install rich` for much nicer event debug printout.')

if TYPE_CHECKING:
    from napari.utils.events.event import Event


class EventDebugSettings(BaseSettings):
    """Parameters controlling how event debugging logs appear.

    To enable Event debugging:
        1. export NAPARI_DEBUG_EVENTS=1  # or modify the .env_sample file
        2. see .env_sample file for ways to set these fields here.
    """

    # event emitters (e.g. 'Shapes') and event names (e.g. 'set_data')
    # to include/exclude when printing events.
    include_emitters: set[str] = Field(default_factory=set)
    include_events: set[str] = Field(default_factory=set)
    exclude_emitters: set[str] = Field(
        default_factory=lambda: {'TransformChain', 'Context'}
    )
    exclude_events: set[str] = Field(
        default_factory=lambda: {'status', 'position'}
    )
    # stack depth to show
    stack_depth: int = 20
    # how many sub-emit nesting levels to show
    # (i.e. events that get triggered by other events)
    nesting_allowance: int = 0

    _cur_depth: int = PrivateAttr(default=0)

    model_config = SettingsConfigDict(
        env_prefix='event_debug_',
        env_file='.env',
    )


_SETTINGS = EventDebugSettings()
_SP = site.getsitepackages()[0]
_STD_LIB = site.__file__.rsplit(os.path.sep, 1)[0]


def _shorten_fname(fname: str) -> str:
    """Reduce extraneous stuff from filenames"""
    return _native.event_shorten_filename(fname)


def log_event_stack(
    event: 'Event', cfg: EventDebugSettings = _SETTINGS
) -> None:
    """Print info about what caused this event to be emitted.s"""

    _native.event_trace(event, cfg)


def _make_pop_source(event: 'Event', cfg: EventDebugSettings):
    """Keep the native cleanup callback's Python references visible to GC."""
    def _pop_source():
        return _native.event_trace_pop(event, cfg)

    return _pop_source
