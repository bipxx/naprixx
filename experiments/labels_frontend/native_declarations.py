"""Compile verified transport doc metadata before creating native declarations.

No application policy is translated. Docstrings are placed into transport ASTs
so CPython emits correct constant indices for generator returns and exceptions.
"""
import ast

from compiler_controls import controls
from freeze_transport import profile,shells
from inspect_inputs import STAGE,load_inputs,walk


def tuples(value):
    return tuple(tuples(item) for item in value) if isinstance(value,list) else value


def prepare_table(probe):
    frozen=controls()
    originals=tuple(row[1] for row in load_inputs())
    path=STAGE/'frontend_shells.py'
    tree=ast.parse(path.read_bytes(),filename=str(path))
    for node in ast.walk(tree):
        if not isinstance(node,ast.FunctionDef) or not node.name.startswith('f') or not node.name[1:].isdigit():
            continue
        entry=int(node.name[1:])
        if entry>=50: continue
        constants=originals[entry].co_consts
        doc=constants[0] if constants and isinstance(constants[0],str) else None
        if doc is not None:
            node.body.insert(0,ast.copy_location(ast.Expr(value=ast.Constant(doc)),node.body[0]))
    ast.fix_missing_locations(tree)
    compiled=list(walk(compile(tree,str(path),'exec',dont_inherit=True)))
    transports=tuple(next(code for code in compiled if code.co_name==f'f{i}') for i in range(50))
    transport_profiles=tuples([profile(code) for code in transports])
    expression_shells=tuple(shells(STAGE/'expression_shells.py','e',7))
    expression_profiles=tuples([row['shell_profile_18'] for row in frozen['expression_rows']])
    source_profiles=tuples([row['original_profile_18'] for row in frozen['frontend_rows']])
    return probe.prepare_native_declaration_table_probe(originals,transports,source_profiles,
        transport_profiles,expression_shells,expression_profiles)
