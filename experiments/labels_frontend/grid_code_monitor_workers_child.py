"""Retire one ended worker while a second worker still owns its scope."""
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
    tracing='--trace' in sys.argv;first=1 if '--reverse' in sys.argv else 0
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(c for c in walk(compile(data,str(path),'exec',dont_inherit=True)) if c.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec'),shells)
    holder={};refs=[];outcomes=[];failures=[];counts=[]
    ready=[threading.Event(),threading.Event()];release=[threading.Event(),threading.Event()]
    def worker_trace(frame,event,arg):return worker_trace
    def main_trace(frame,event,arg):return main_trace
    def foreign(current,*args):
        if current in holder.values():raise RuntimeError('workers foreign unwind')
    def broken(*args,**kwargs):raise RuntimeError('workers provider failure')
    class Receiver:sparse=True
    def worker(index):
        try:
            namespace={'__builtins__':vars(builtins),'math':math,'_nx':types.SimpleNamespace(arange=broken)}
            function=types.FunctionType(code,namespace);wrapper=LineProbe(probe,opcodes=True,completion=True)
            function.__code__=wrapper.prepare_grid_function_probe(function,namespace,shells['_grid_getitem'],
                tuples(profile(code)),tuples(profile(shells['_grid_getitem'].__code__)),0,
                shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
            holder[index]=function.__code__;receiver=Receiver()
            refs.extend((weakref.ref(function),weakref.ref(function.__code__),weakref.ref(receiver)))
            probe.set_grid_code_monitor_probe(function.__code__,True)
            if tracing:sys.settrace(worker_trace)
            try:function(receiver,slice(1,5,2))
            except RuntimeError as error:outcomes.append((index,str(error)))
            else:raise AssertionError('foreign unwind did not fail')
            ready[index].set();assert release[index].wait(10)
        except BaseException as error:failures.append((type(error).__name__,str(error)))
        finally:sys.settrace(None);ready[index].set()
    def reject(index):
        try:probe.set_grid_code_monitor_probe(holder[index],False)
        except ValueError as error:assert str(error)=='code monitor is active'
        else:raise AssertionError('live worker scope was removed')
    monitor=sys.monitoring;probe.start_grid_code_monitor_probe(3)
    monitor.use_tool_id(5,'concurrent workers fixture')
    monitor.register_callback(5,monitor.events.PY_UNWIND,foreign);monitor.set_events(5,monitor.events.PY_UNWIND)
    threads=[threading.Thread(target=worker,args=(index,)) for index in range(2)]
    for thread in threads:thread.start()
    try:
        assert all(event.wait(10) for event in ready) and not failures,failures
        counts.append(tuple(probe.grid_code_monitor_counts_probe()));assert counts[-1]==(3,2,2)
        reject(0);reject(1)
        release[first].set();threads[first].join(10)
        assert not threads[first].is_alive() and threads[1-first].is_alive()
        sys.settrace(main_trace)
        try:
            probe.set_grid_code_monitor_probe(holder[first],False);holder.pop(first)
            counts.append(tuple(probe.grid_code_monitor_counts_probe()));assert counts[-1]==(3,1,1)
            reject(1-first);assert sys.gettrace() is main_trace
        finally:sys.settrace(None)
    finally:
        for event in release:event.set()
        for thread in threads:thread.join(10)
    assert all(not thread.is_alive() for thread in threads) and not failures,failures
    monitor.set_events(5,0);monitor.register_callback(5,monitor.events.PY_UNWIND,None);monitor.free_tool_id(5)
    probe.set_grid_code_monitor_probe(holder[1-first],False);holder.clear()
    probe.stop_grid_code_monitor_probe();gc.collect()
    assert sorted(outcomes)==[(0,'workers foreign unwind'),(1,'workers foreign unwind')]
    assert len(refs)==6 and all(item() is None for item in refs)
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    print(json.dumps({'status':'PASS_FINITE_CONCURRENT_WORKERS','controls':1,'tracing':tracing,'first_exit':first,
        'counts':counts,'live_worker_removal_rejected':True,'main_tracer_preserved':True,
        'functions_codes_receivers_released':True,'tool_and_scopes_released':True,
        'complete_thread_module_shutdown_abi_accepted':False,'python':sys.version}));return 0


if __name__=='__main__':raise SystemExit(main())
