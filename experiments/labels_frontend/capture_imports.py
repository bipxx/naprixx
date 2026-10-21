"""Experimental first-import declaration transport, not an application loader.

Only an innermost native capture decorator is added. Removing those markers must
recover the complete original AST, including locations. Function bodies remain
Python in this import oracle; admission/publication into C++ is a separate step.
"""
import ast
import copy
import dis
import hashlib
import importlib.abc
import importlib.machinery
import json
from pathlib import Path
import sys
from types import CodeType
import weakref

from inspect_inputs import ROOT, STAGE, walk

MARKER='naparixx.producer.marker.v1'


def is_marker(node):
    return (isinstance(node,ast.Call) and isinstance(node.func,ast.Attribute)
            and isinstance(node.func.value,ast.Constant) and node.func.value.value==MARKER)


def instrument(path,definitions,source=None):
    original=ast.parse(path.read_bytes() if source is None else source,filename=str(path))
    tree=copy.deepcopy(original)
    entries={(row['qualname'],row['occurrence']):entry for entry,row in definitions}
    observed=set()
    class Inject(ast.NodeTransformer):
        prefix=''
        counts={}
        def mark(self,node,key):
            if key in entries:
                entry=entries[key]
                marker=ast.Call(func=ast.Attribute(value=ast.Constant(MARKER),attr='__call__',ctx=ast.Load()),
                                args=[ast.Constant(entry)],keywords=[])
                node.decorator_list.append(ast.copy_location(marker,node))
                observed.add(entry)
        def visit_ClassDef(self,node):
            before=self.prefix
            qualname=before+node.name
            occurrence=self.counts.get(qualname,0)+1
            self.counts[qualname]=occurrence
            self.prefix=qualname+'.'
            self.generic_visit(node)
            self.prefix=before
            self.mark(node,(qualname,occurrence))
            return node
        def visit_FunctionDef(self,node):
            qualname=self.prefix+node.name
            occurrence=self.counts.get(qualname,0)+1
            self.counts[qualname]=occurrence
            before=self.prefix
            self.prefix=qualname+'.<locals>.'
            self.generic_visit(node)
            self.prefix=before
            key=(qualname,occurrence)
            self.mark(node,key)
            return node
    tree=Inject().visit(tree)
    ast.fix_missing_locations(tree)
    recovered=copy.deepcopy(tree)
    for node in ast.walk(recovered):
        if isinstance(node,(ast.FunctionDef,ast.AsyncFunctionDef,ast.ClassDef)):
            node.decorator_list=[decorator for decorator in node.decorator_list if not is_marker(decorator)]
    assert ast.dump(recovered,include_attributes=True)==ast.dump(original,include_attributes=True)
    assert observed=={entry for entry,_ in definitions}
    code=compile(tree,str(path),'exec',dont_inherit=True)
    sites={}
    for parent in walk(code):
        instructions=list(dis.get_instructions(parent))
        for index,instruction in enumerate(instructions):
            if instruction.opname!='LOAD_CONST' or instruction.argval!=MARKER:
                continue
            entry=next(item.argval for item in instructions[index+1:] if item.opname=='LOAD_CONST')
            call=next(offset for offset in range(index+1,len(instructions)) if instructions[offset].opname=='CALL')
            make=next(offset for offset in range(call+1,len(instructions)) if instructions[offset].opname=='MAKE_FUNCTION')
            apply=next(offset for offset in range(make+1,len(instructions)) if instructions[offset].opname=='CALL')
            if entry>=50:  # The first call builds the class; the next applies its innermost decorator.
                apply=next(offset for offset in range(apply+1,len(instructions)) if instructions[offset].opname=='CALL')
            assert entry not in sites
            sites[entry]=(instructions[call].offset,instructions[call+1].offset,
                          instructions[apply].offset,instructions[apply+1].offset)
    assert set(sites)==observed
    rows=tuple((entry,row['qualname'],row['occurrence'],*sites[entry]) for entry,row in definitions)
    return code,rows


class CaptureSession:
    def __init__(self,probe,groups,native_table=None):
        self.probe=probe
        self.groups=groups
        self.scopes={}
        self.original_loaders={}
        self.native_table=native_table


class CaptureLoader(importlib.machinery.SourceFileLoader):
    def __init__(self,fullname,path,session):
        super().__init__(fullname,path)
        self._session=weakref.ref(session)

    def get_code(self,fullname):
        session=self._session()
        if session is None:
            return super().get_code(fullname)
        path=Path(self.path).resolve()
        definitions=session.groups[fullname]
        assert path==(ROOT/definitions[0][1]['module']).resolve()
        source=path.read_bytes()
        assert hashlib.sha256(source).hexdigest()==definitions[0][1]['source']['sha256']
        code,rows=instrument(path,definitions,source)
        original=compile(source,str(path),'exec',dont_inherit=True)
        if session.native_table is None:
            owner=session.probe.prepare_producer_capture_probe(sys.modules[fullname],code,rows,original)
        else:
            owner=session.probe.prepare_native_first_capture_probe(sys.modules[fullname],code,rows,original,session.native_table)
        session.scopes[fullname]=owner
        return owner.code


class CaptureFinder(importlib.abc.MetaPathFinder):
    def __init__(self,session):
        self.session=session

    def find_spec(self,fullname,path=None,target=None):
        if fullname not in self.session.groups:
            return None
        spec=importlib.machinery.PathFinder.find_spec(fullname,path)
        assert spec is not None and isinstance(spec.loader,importlib.machinery.SourceFileLoader)
        self.session.original_loaders[fullname]=spec.loader
        spec.loader=CaptureLoader(fullname,spec.origin,self.session)
        return spec


def import_captured(probe,native_table=None):
    manifest=json.loads((STAGE/'manifest.json').read_text())
    for name,expected in manifest['source_inputs'].items():
        assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==expected, name
    selection=json.loads((STAGE/'selection.json').read_text())
    groups={}
    for entry,row in enumerate(selection['definitions']):
        name=row['module'][4:-3].replace('/','.')
        groups.setdefault(name,[]).append((entry,row))
    for entry,name,qualname in (
        (50,'napari.layers.labels.labels','Labels'),
        (51,'napari.layers.labels.labels','WrongSelectedLabelError'),
        (52,'napari.layers.labels._labels_mouse_bindings','BrushSizeOnMouseMove'),
        (53,'napari.layers.base.base','Layer'),
    ):
        groups[name].append((entry,dict(module=groups[name][0][1]['module'],qualname=qualname,occurrence=1)))
    assert not set(groups).intersection(sys.modules), 'Capture requires genuinely fresh source imports'
    session=CaptureSession(probe,groups,native_table)
    finder=CaptureFinder(session)
    sys.meta_path.insert(0,finder)
    try:
        try:
            from napari.layers import Labels
            from napari.utils import interactions
            assert set(session.scopes)==set(groups)
        finally:
            sys.meta_path.remove(finder)
            for name,loader in session.original_loaders.items():
                module=sys.modules.get(name)
                if module is not None:
                    module.__loader__=loader
                    module.__spec__.loader=loader
        for owner in session.scopes.values():
            owner.finish()
        return tuple(session.scopes.values())
    finally:
        # Real importlib frames can outlive this call through provider error
        # tracebacks. Do not leave producer owners in their caller's locals.
        session.scopes.clear()
        session.native_table=None
        native_table=None
        owner=None
