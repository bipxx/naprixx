"""Recover a skipped monitoring exit after the owning worker has ended."""
import gc,hashlib,json,sys,threading,weakref
from pathlib import Path
from grid_comprehension_child import deny_archive
from grid_line_transport import LineProbe
from freeze_transport import profile
from native_declarations import tuples
from inspect_inputs import walk
import grid_getitem_locals_child as getter

DIRECTORY=Path(__file__).resolve().parent;ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        print(json.dumps({'status':'OPEN_CP311_CODE_MONITOR','controls':0}));return 0
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(c for c in walk(compile(data,str(path),'exec',dont_inherit=True)) if c.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec'),shells)
    tracing='--trace' in sys.argv;live='--live' in sys.argv
    monitor=sys.monitoring;holder={};failures=[];refs=[];outcomes=[]
    ready=threading.Event();release=threading.Event()
    def worker_trace(frame,event,arg):return worker_trace
    def main_trace(frame,event,arg):return main_trace
    def foreign(current,*args):
        if current is holder.get('code'):raise RuntimeError('worker foreign unwind')
    def setup(namespace):
        def broken(*args,**kwargs):raise RuntimeError('worker provider failure')
        namespace['_nx'].arange=broken
    def invoke(function,args):
        wrapper=LineProbe(probe,opcodes=True,completion=True)
        function.__code__=wrapper.prepare_grid_function_probe(function,function.__globals__,shells['_grid_getitem'],
            tuples(profile(code)),tuples(profile(shells['_grid_getitem'].__code__)),0,
            shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
        holder['code']=function.__code__;refs.extend((weakref.ref(function),weakref.ref(function.__code__)))
        probe.set_grid_code_monitor_probe(function.__code__,True)
        try:function(*args)
        except RuntimeError as error:
            assert str(error)=='worker foreign unwind'
            outcomes.append(str(error));return None
        raise AssertionError('foreign unwind did not fail')
    def worker():
        try:
            if tracing:sys.settrace(worker_trace)
            getter.run(probe,code,shells,'single',False,invoke=invoke,namespace_setup=setup)
            ready.set();assert release.wait(10)
        except BaseException as error:failures.append((type(error).__name__,str(error)))
        finally:sys.settrace(None);ready.set()
    probe.start_grid_code_monitor_probe(3)
    monitor.use_tool_id(5,'worker foreign fixture')
    monitor.register_callback(5,monitor.events.PY_UNWIND,foreign)
    monitor.set_events(5,monitor.events.PY_UNWIND)
    thread=threading.Thread(target=worker);thread.start();assert ready.wait(10)
    if live:
        assert not failures and thread.is_alive(),failures
        assert tuple(probe.grid_code_monitor_counts_probe())==(3,1,1)
        try:probe.set_grid_code_monitor_probe(holder['code'],False)
        except ValueError as error:assert str(error)=='code monitor is active'
        else:raise AssertionError('live worker scope was removed on another thread')
        assert tuple(probe.grid_code_monitor_counts_probe())==(3,1,1)
    release.set();thread.join(10)
    assert not thread.is_alive() and not failures,failures
    monitor.set_events(5,0);monitor.register_callback(5,monitor.events.PY_UNWIND,None);monitor.free_tool_id(5)
    before=tuple(probe.grid_code_monitor_counts_probe())
    try:
        sys.settrace(main_trace)
        try:
            probe.set_grid_code_monitor_probe(holder['code'],False)
            probe.stop_grid_code_monitor_probe()
            assert sys.gettrace() is main_trace
        finally:sys.settrace(None)
    except ValueError as error:
        print(json.dumps({'status':'OPEN_EXITED_THREAD_SCOPE','controls':1,'counts_after_join':before,
            'error':str(error),'outcomes':outcomes}));return 1
    holder.clear();gc.collect()
    assert outcomes==['worker foreign unwind'] and len(refs)==2 and all(item() is None for item in refs)
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    print(json.dumps({'status':'PASS_FINITE_EXITED_THREAD_RECOVERY','controls':1,'counts_after_join':before,
        'function_and_code_released':True,'tool_and_scopes_released':True,
        'worker_tracing':tracing,'live_worker_removal_rejected':live,'main_tracer_preserved':True,
        'complete_thread_module_shutdown_abi_accepted':False,'python':sys.version}));return 0


if __name__=='__main__':raise SystemExit(main())
