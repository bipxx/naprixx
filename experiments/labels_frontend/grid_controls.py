"""Verify the selected NumPy policy sources before compiling inert controls."""
import hashlib
import json
from pathlib import Path

import numpy
from freeze_transport import profile
from inspect_inputs import STAGE,walk
from native_declarations import tuples


def prepare_grid(probe):
    pins=json.loads((STAGE/'grid_provider_controls.json').read_text())
    package_root=Path(numpy.__file__).resolve().parent.parent
    originals=[]
    for row in pins['sources']:
        path=package_root/row['package_path']
        source=path.read_bytes()
        assert hashlib.sha256(source).hexdigest()==row['sha256'],str(path)
        codes=list(walk(compile(source,str(path),'exec',dont_inherit=True)))
        originals.append(next(code for code in codes if code.co_qualname==row['qualname']))
    path=STAGE/'grid_shells.py'
    namespace={}
    exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),namespace)
    shells=namespace['_grid_getitem'],namespace['_grid_indices']
    return probe.prepare_grid_provider_plan_probe(shells,tuples([profile(code) for code in originals]),
        tuples([profile(function.__code__) for function in shells]),
        namespace['_grid_sparse_comp'],tuples(profile(namespace['_grid_sparse_comp'].__code__)))
