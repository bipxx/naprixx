"""A retired frame's finalizer stops the monitor during an outer removal."""
import builtins,gc,hashlib,json,math,sys,threading,types,weakref
from pathlib import Path
from grid_comprehension_child import deny_archive
from grid_line_transport import LineProbe
from freeze_transport import profile
from native_declarations import tuples
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent;ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        print(json.dumps({'status':'OPEN_CP311_CODE_MONITOR','controls':0}));return 0
    tracing='--trace' in sys.argv;remove_owner='--remove-owner' in sys.argv
    restart='--restart' in sys.argv
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(c for c in walk(compile(data,str(path),'exec',dont_inherit=True)) if c.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec'),shells)
    holder={};refs=[];outcomes=[];finalized=[];failures=[]
    def foreign(current,*args):
        if current is holder.get('code'):raise RuntimeError('cleanup foreign unwind')
    def broken(*args,**kwargs):raise RuntimeError('cleanup provider failure')
    def worker_trace(frame,event,arg):return worker_trace
    def main_trace(frame,event,arg):return main_trace
    class Receiver:
        sparse=True
        def __del__(self):
            try:
                assert threading.current_thread() is threading.main_thread()
                probe.set_grid_code_monitor_probe(holder['code'],False)
                probe.stop_grid_code_monitor_probe()
                if restart:probe.start_grid_code_monitor_probe(3)
                if remove_owner:del probe._grid_code_monitor_owner
                finalized.append('stopped')
            except BaseException as error:finalized.append((type(error).__name__,str(error)))
    def worker():
        try:
            namespace={'__builtins__':vars(builtins),'math':math,'_nx':types.SimpleNamespace(arange=broken)}
            function=types.FunctionType(code,namespace)
            wrapper=LineProbe(probe,opcodes=True,completion=True)
            function.__code__=wrapper.prepare_grid_function_probe(function,namespace,shells['_grid_getitem'],
                tuples(profile(code)),tuples(profile(shells['_grid_getitem'].__code__)),0,
                shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
            holder['code']=function.__code__
            receiver=Receiver();refs.extend((weakref.ref(function),weakref.ref(function.__code__),weakref.ref(receiver)))
            probe.set_grid_code_monitor_probe(function.__code__,True)
            if tracing:sys.settrace(worker_trace)
            try:function(receiver,slice(1,5,2))
            except RuntimeError as error:outcomes.append(str(error))
            else:raise AssertionError('foreign unwind did not fail')
        except BaseException as error:failures.append((type(error).__name__,str(error)))
        finally:sys.settrace(None)
    monitor=sys.monitoring;probe.start_grid_code_monitor_probe(3)
    monitor.use_tool_id(5,'cleanup finalizer fixture')
    monitor.register_callback(5,monitor.events.PY_UNWIND,foreign);monitor.set_events(5,monitor.events.PY_UNWIND)
    thread=threading.Thread(target=worker);thread.start();thread.join(10)
    assert not thread.is_alive() and not failures,failures
    monitor.set_events(5,0);monitor.register_callback(5,monitor.events.PY_UNWIND,None);monitor.free_tool_id(5)
    assert outcomes==['cleanup foreign unwind'] and not finalized
    before=tuple(probe.grid_code_monitor_counts_probe());assert before==(3,1,1)
    sys.settrace(main_trace)
    try:
        try:probe.set_grid_code_monitor_probe(holder['code'],False)
        except ValueError as error:outer_error=str(error)
        else:raise AssertionError('outer removal ignored the reentrant stop')
        assert sys.gettrace() is main_trace
    finally:sys.settrace(None)
    assert finalized==['stopped'],finalized
    assert outer_error==('code monitor session changed during cleanup' if restart else 'code monitor is not started'),outer_error
    if restart:
        assert tuple(probe.grid_code_monitor_counts_probe())==(3,0,0)
        probe.stop_grid_code_monitor_probe()
    holder.clear();gc.collect()
    assert len(refs)==3 and all(item() is None for item in refs)
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    assert monitor.get_tool(3) is None and monitor.get_events(3)==0
    print(json.dumps({'status':'PASS_FINITE_REENTRANT_CLEANUP','controls':1,
        'worker_tracing':tracing,'module_owner_removed':remove_owner,'session_restarted':restart,'counts_before_cleanup':before,
        'outer_error':outer_error,'finalizer_ran_once':True,'function_code_receiver_released':True,
        'main_tracer_preserved':True,'tool_and_scopes_released':True,
        'complete_module_thread_shutdown_abi_accepted':False,'python':sys.version}));return 0


if __name__=='__main__':raise SystemExit(main())
