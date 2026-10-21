import os

if os.name != "nt":
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest

import naparixx


@pytest.fixture(autouse=True)
def restore_backend():
    naparixx.disable()
    yield
    naparixx.disable()
