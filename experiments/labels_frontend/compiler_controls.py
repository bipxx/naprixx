"""Build compiler controls from byte-verified sources without executing them."""
import hashlib
import json
from pathlib import Path
import types

from inspect_inputs import ROOT, STAGE, load_inputs
from freeze_transport import profile, shells


def controls():
    manifest=json.loads((STAGE/'manifest.json').read_text())
    for name,expected in manifest['source_inputs'].items():
        assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==expected, name
    for name,expected in manifest['experiment_inputs'].items():
        assert hashlib.sha256((STAGE/name).read_bytes()).hexdigest()==expected, name
    inputs=load_inputs()
    frontend=shells(STAGE/'frontend_shells.py','f',50)
    expressions=shells(STAGE/'expression_shells.py','e',7)
    rows=[]
    for (_,original,_),shell in zip(inputs,frontend,strict=True):
        rows.append(dict(original_profile_18=profile(original),shell_profile_18=profile(shell)))
    expression_rows=[]
    mapping=((10,0),(10,1),(11,0),(29,0),(3,0),(3,1),(3,2))
    for (parent,ordinal),shell in zip(mapping,expressions,strict=True):
        children=[value for value in inputs[parent][1].co_consts
                  if isinstance(value,types.CodeType) and value.co_name=='<genexpr>']
        original=children[ordinal]
        assert original.co_freevars==shell.co_freevars
        expression_rows.append(dict(original_profile_18=profile(original),shell_profile_18=profile(shell)))
    return dict(frontend_rows=rows,expression_rows=expression_rows)
