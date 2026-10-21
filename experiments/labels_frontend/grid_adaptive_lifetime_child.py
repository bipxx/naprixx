"""Code-extra observer state must not retain original or prepared code objects."""
import builtins
import gc
import hashlib
import json
from pathlib import Path
import sys
import types
import weakref

from grid_comprehension_child import deny_archive
from freeze_transport import profile
from grid_line_transport import LineProbe
from inspect_inputs import walk
from native_declarations import tuples

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][1]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    original=weakref.ref(code)
    namespace={'__name__':'numpy._core.numeric','__builtins__':vars(builtins)}
    function=types.FunctionType(code,namespace)
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    shell=shells['_grid_indices'];wrapper=LineProbe(probe,opcodes=True)
    function.__code__=wrapper.prepare_grid_function_probe(function,namespace,shell,
        tuples(profile(code)),tuples(profile(shell.__code__)),1)
    del code;gc.collect();assert original() is None
    prepared=weakref.ref(function.__code__);alias=types.FunctionType(function.__code__,namespace)
    assert wrapper.invoke(function,((),int,True))==()
    del function;gc.collect();assert prepared() is alias.__code__
    assert wrapper.invoke(alias,((),int,True))==()
    del alias;gc.collect();assert prepared() is None
    class Array:
        def __getattribute__(self,name):return object.__getattribute__(self,name)
        def reshape(self,shape):return tuple(shape)
    source_path=ROOT/'.venv/Lib/site-packages'/row['package_path']
    code=next(item for item in walk(compile(data,str(source_path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    original=weakref.ref(code)
    namespace={'__name__':'numpy._core.numeric','__builtins__':vars(builtins),'arange':lambda *a,**kw:Array()}
    function=types.FunctionType(code,namespace)
    for _ in range(8):assert function((1,),int,True)==((1,),)
    function.__code__=wrapper.prepare_grid_function_probe(function,namespace,shell,
        tuples(profile(code)),tuples(profile(shell.__code__)),1)
    del code;gc.collect();assert original() is None
    prepared=weakref.ref(function.__code__)
    assert wrapper.invoke(function,((1,),int,True))==((1,),)
    del function;gc.collect();assert prepared() is None
    print(json.dumps({'status':'PASS_FINITE_NATIVE_ADAPTIVE_CODE_LIFETIME','controls':5,'python':sys.version,
        'complete_lifetime_abi_accepted':False,'default_application_enabled':False}))


if __name__=='__main__':main()
