"""Source comparisons for caught len errors and scalar grid fallback tracing."""
import ctypes
import builtins
import hashlib
import json
from pathlib import Path
import sys

from grid_comprehension_child import deny_archive
import grid_getitem_locals_child as getter
from grid_line_transport import LineProbe
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]
MODES=('single','complex-single','single-none-start','complex-single-one',
       'handler-mutation','exception-disable','handler-lines-off',
       'builtin-len','bound-len','callable-len','index-error',
       'arange-error','complex-arange-error','arange-exception-disable')


def run(probe,code,shells,mode,native):
    events=[];changed=False;wrapper=LineProbe(probe)
    owner=(code.co_filename,code.co_qualname)
    def setup(namespace):
        class Length:
            def __call__(self,value): return builtins.len(value)
        def index_error(value): raise IndexError('length callback failure')
        def arange_error(*args,**kwargs): raise RuntimeError('fallback arange failure')
        if mode=='builtin-len': namespace['len']=builtins.len
        elif mode=='bound-len': namespace['len']=Length().__call__
        elif mode=='callable-len': namespace['len']=Length()
        elif mode=='index-error': namespace['len']=index_error
        elif mode in ('arange-error','complex-arange-error','arange-exception-disable'): namespace['_nx'].arange=arange_error
    def error_summary(error):
        return None if error is None else (type(error).__name__,str(error))
    def traceback_positions(tb):
        items=[]
        while tb:
            if (tb.tb_frame.f_code.co_filename,tb.tb_frame.f_code.co_qualname)==owner:
                items.append((tb.tb_lineno,tb.tb_lasti))
            tb=tb.tb_next
        return tuple(items)
    def invoke(function,args):
        try: return wrapper.invoke(function,args) if native else function(*args)
        except RuntimeError as error:
            assert mode in ('arange-error','complex-arange-error','arange-exception-disable')
            context=error.__context__
            return ('caught',error_summary(error),traceback_positions(error.__traceback__),
                    error_summary(context),traceback_positions(context.__traceback__))
    def trace(frame,event,arg):
        nonlocal changed
        if (frame.f_code.co_filename,frame.f_code.co_qualname)!=owner:return None
        actual=None
        if event=='exception':
            actual=(arg[0].__name__,str(arg[1]),traceback_positions(arg[2]))
        events.append((event,frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals)),actual,error_summary(sys.exception())))
        if event=='exception' and (mode=='exception-disable' or
                mode=='arange-exception-disable' and isinstance(arg[1],RuntimeError)):
            changed=True;sys.settrace(None)
        elif mode=='handler-lines-off' and event=='line' and frame.f_lineno==193:
            changed=True;frame.f_trace_lines=False
        elif mode=='handler-mutation' and event=='line' and frame.f_lineno==194:
            changed=True;values=frame.f_locals;values['key']=type(values['key'])(0,4,1)
            if sys.version_info<(3,13):
                sync=ctypes.pythonapi.PyFrame_LocalsToFast
                sync.argtypes,sync.restype=[ctypes.py_object,ctypes.c_int],None;sync(frame,1)
        return trace
    sys.settrace(trace)
    try:
        result=getter.run(wrapper if native else probe,code,shells,
            mode if mode in ('single','complex-single','single-none-start','complex-single-one') else
                'complex-single' if mode=='complex-arange-error' else 'single',
            native,invoke=invoke,namespace_setup=setup)
        assert sys.gettrace() is (None if mode in ('exception-disable','arange-exception-disable') else trace)
    finally:sys.settrace(None)
    if mode in ('handler-mutation','exception-disable','handler-lines-off','arange-exception-disable'):assert changed,mode
    assert any(item[0]=='exception' for item in events)
    return result,events


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    path=DIRECTORY/'grid_shells.py';shells={};exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    for mode in MODES:
        before=run(probe,code,shells,mode,False);after=run(probe,code,shells,mode,True)
        assert before==after,(mode,before,after)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_CAUGHT_FALLBACK_TRACING','controls':len(MODES),'python':sys.version,
        'complete_exception_and_frame_parity_accepted':False,'default_application_enabled':False}))


if __name__=='__main__':main()
