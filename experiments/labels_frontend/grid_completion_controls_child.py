"""Run existing source comparisons with the opt-in completion bridge selected."""
from pathlib import Path
import runpy
import sys

import grid_line_transport as transport

CONTROLS={
    'grid_retained_frame_child.py','grid_adaptive_lifetime_child.py',
    'grid_position_reuse_child.py','grid_provider_traced_position_child.py',
    'grid_observer_error_child.py','grid_fallback_opcode_child.py',
    'grid_getter_opcode_child.py','grid_fallback_child.py',
    'grid_getter_line_child.py','grid_opcode_child.py','grid_line_child.py',
    'grid_property_position_child.py','grid_adaptive_mutation_child.py',
    'grid_hot_handoff_child.py',
}


def main():
    name=sys.argv[1]
    assert name in CONTROLS
    original=transport.LineProbe
    class CompletionProbe(original):
        def __init__(self,probe,opcodes=False,completion=False):
            super().__init__(probe,opcodes=opcodes,completion=True)
    transport.LineProbe=CompletionProbe
    path=Path(__file__).resolve().parent/name
    sys.argv=[str(path),*sys.argv[2:]]
    runpy.run_path(str(path),run_name='__main__')


if __name__=='__main__':main()
