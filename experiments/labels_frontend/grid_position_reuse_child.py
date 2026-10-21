"""Repeated native calls must not inherit a recycled frame's observer cursor."""
import builtins
import hashlib
import json
from pathlib import Path
import sys
import types

from grid_comprehension_child import deny_archive
from freeze_transport import profile
from grid_line_transport import LineProbe
from inspect_inputs import walk
from native_declarations import tuples

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


def run(probe,code,shell,native):
    namespace={'__name__':'numpy._core.numeric','__builtins__':vars(builtins)}
    function=types.FunctionType(code,namespace);wrapper=LineProbe(probe,opcodes=True)
    if native:
        function.__code__=wrapper.prepare_grid_function_probe(function,namespace,shell,
            tuples(profile(code)),tuples(profile(shell.__code__)),1)
        def guard(frame,event,arg):
            if event=='call' and frame.f_code is code:raise AssertionError('original indices body executed')
        sys.setprofile(guard)
    def repeated():
        result=[]
        for _ in range(100):result.append(function((),int,True))
        return result
    try:result=wrapper.invoke(repeated,()) if native else repeated()
    finally:sys.setprofile(None)
    assert sys.gettrace() is None and result==[()]*100
    return result


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][1]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    assert run(probe,code,shells['_grid_indices'],False)==run(probe,code,shells['_grid_indices'],True)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_FRAME_REUSE','native_calls':100,'python':sys.version,
        'complete_frame_abi_accepted':False,'default_application_enabled':False}))


if __name__=='__main__':main()
