"""Compare failures raised by Python trace callbacks against genuine source."""
import dis
import ctypes
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
MODES=('call-runtime','line-runtime','opcode-runtime','exception-runtime',
       'return-runtime','handler-line-runtime','handler-opcode-runtime',
       'line-typeerror','opcode-typeerror','line-baseexception','return-typeerror',
       'ambient-line-runtime','handler-entry-runtime','return-opcode-runtime')


def run(probe,code,shells,mode,native):
    wrapper=LineProbe(probe,opcodes=True);events=[];frames=[];changed=False
    owner=(code.co_filename,code.co_qualname)
    decoded={item.offset:item for item in dis.get_instructions(code)}
    def summary(error):return None if error is None else (type(error).__name__,str(error))
    def positions(tb):
        result=[]
        while tb:
            if (tb.tb_frame.f_code.co_filename,tb.tb_frame.f_code.co_qualname)==owner:
                result.append((tb.tb_lineno,tb.tb_lasti))
            tb=tb.tb_next
        return tuple(result)
    def invoke(function,args):
        try:return wrapper.invoke(function,args) if native else function(*args)
        except BaseException as error:
            chain=[];seen=set();item=error
            while item is not None and id(item) not in seen:
                seen.add(id(item));chain.append((summary(item),positions(item.__traceback__)))
                item=item.__context__
            return ('raised',tuple(chain))
    def trace(frame,event,arg):
        nonlocal changed
        if (frame.f_code.co_filename,frame.f_code.co_qualname)!=owner:return None
        if event=='call':frame.f_trace=trace;frame.f_trace_opcodes=True;frames.append(frame)
        detail=(summary(arg[1]),positions(arg[2])) if event=='exception' else None
        events.append((event,frame.f_lineno,frame.f_lasti,tuple(sorted(frame.f_locals)),summary(sys.exception()),detail))
        target=False
        if not changed:
            if mode=='call-runtime':target=event=='call'
            elif mode=='exception-runtime':target=event=='exception'
            elif mode.startswith('return-'):target=event=='return'
            elif mode in ('line-runtime','ambient-line-runtime'):target=event=='line' and frame.f_lineno==153
            elif mode in ('line-typeerror','line-baseexception'):target=event=='line' and frame.f_lineno==152
            elif mode=='handler-line-runtime':target=event=='line' and frame.f_lineno==194
            elif mode=='handler-entry-runtime':target=event=='opcode' and decoded[frame.f_lasti].opname=='PUSH_EXC_INFO'
            elif mode=='return-opcode-runtime':target=event=='opcode' and decoded[frame.f_lasti].opname=='RETURN_VALUE'
            elif mode in ('opcode-runtime','handler-opcode-runtime','opcode-typeerror') and event=='opcode':
                instruction=decoded[frame.f_lasti]
                target=frame.f_lineno==(194 if mode=='handler-opcode-runtime' else 152 if mode=='opcode-typeerror' else 153)
                target=target and (instruction.opname=='LOAD_GLOBAL' if mode=='opcode-typeerror' else instruction.opname.startswith('LOAD_FAST'))
        if target:
            changed=True
            error=TypeError if mode.endswith('typeerror') else SystemExit if mode=='line-baseexception' else RuntimeError
            raise error('observer failure: '+mode)
        return trace
    baseline=sys.exception()
    controller=sys._getframe();previous=controller.f_trace_opcodes;controller.f_trace_opcodes=True
    sys.settrace(trace)
    try:
        def call():
            keymode='single' if mode in ('exception-runtime','handler-line-runtime','handler-opcode-runtime',
                                       'line-typeerror','opcode-typeerror','return-typeerror',
                                       'handler-entry-runtime','return-opcode-runtime') else 'sparse'
            return getter.run(wrapper if native else probe,code,shells,keymode,native,invoke=invoke,allow_empty=True)
        if mode.startswith('ambient-'):
            try:raise LookupError('ambient observer caller')
            except LookupError as ambient:result=call();assert sys.exception() is ambient
        else:result=call()
        assert changed and sys.gettrace() is None,mode
        active=sys.exception();post_context=(summary(active),positions(None if active is None else active.__traceback__))
    finally:
        sys.settrace(None);controller.f_trace_opcodes=previous
        # Record the source's post-call state before isolating the next oracle.
        restore=ctypes.pythonapi.PyErr_SetHandledException
        restore.argtypes,restore.restype=[ctypes.py_object],None;restore(baseline)
    return result,events,tuple((frame.f_trace is None,frame.f_trace_opcodes) for frame in frames),post_context


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    for mode in MODES:
        before=run(probe,code,shells,mode,False);after=run(probe,code,shells,mode,True)
        assert before==after,(mode,before,after)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_OBSERVER_ERRORS','controls':len(MODES),'python':sys.version,
        'complete_observer_abi_accepted':False,'default_application_enabled':False}))


if __name__=='__main__':main()
