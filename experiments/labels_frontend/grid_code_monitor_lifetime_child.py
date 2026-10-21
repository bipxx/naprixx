"""Tool reservation, collision rollback, active removal and weak code release."""
import gc,hashlib,json,sys,weakref
from pathlib import Path
from grid_comprehension_child import deny_archive
from grid_line_transport import LineProbe
from inspect_inputs import walk
import grid_getitem_locals_child as getter

DIRECTORY=Path(__file__).resolve().parent;ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        try:probe.start_grid_code_monitor_probe(3)
        except NotImplementedError:pass
        else:raise AssertionError('unsupported monitor admitted')
        print(json.dumps({'status':'OPEN_CP311_CODE_MONITOR','controls':0}));return 0
    monitoring=sys.monitoring;events=monitoring.events
    def reject(action):
        try:action()
        except ValueError:return
        raise AssertionError('unsafe monitor transition admitted')
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    monitoring.use_tool_id(3,'foreign fixture')
    try:
        reject(lambda:probe.start_grid_code_monitor_probe(3))
        assert monitoring.get_tool(3)=='foreign fixture'
    finally:monitoring.free_tool_id(3)
    def foreign(*args):pass
    monitoring.use_tool_id(3,'retained callback fixture')
    monitoring.register_callback(3,events.PY_START,foreign)
    monitoring.free_tool_id(3)
    reject(lambda:probe.start_grid_code_monitor_probe(3))
    assert monitoring.get_tool(3) is None
    monitoring.use_tool_id(3,'inspect retained callback')
    try:assert monitoring.register_callback(3,events.PY_START,None) is foreign
    finally:monitoring.free_tool_id(3)
    reject(lambda:probe.start_grid_code_monitor_probe(99))
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    probe.start_grid_code_monitor_probe(3)
    reject(lambda:probe.start_grid_code_monitor_probe(3))
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][0]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(c for c in walk(compile(data,str(path),'exec',dont_inherit=True)) if c.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec'),shells)
    refs=[];holder={};active=[]
    def observe(event,frame):
        if active:return
        assert tuple(probe.grid_code_monitor_counts_probe())==(3,1,1)
        reject(lambda:probe.set_grid_code_monitor_probe(holder['code'],False))
        reject(probe.stop_grid_code_monitor_probe)
        active.append(True)
    def invoke(function,args):
        holder['code']=function.__code__
        probe.set_grid_code_monitor_probe(function.__code__,True)
        reject(lambda:probe.set_grid_code_monitor_probe(function.__code__,True))
        try:return function(*args)
        finally:
            holder.clear();function._cycle=function
            refs.extend((weakref.ref(function),weakref.ref(function.__code__)))
    try:
        getter.run(LineProbe(probe,opcodes=True,completion=True),code,shells,'single',True,invoke=invoke,observe=observe)
        gc.collect()
        assert active==[True] and len(refs)==2 and all(ref() is None for ref in refs)
        assert tuple(probe.grid_code_monitor_counts_probe())==(3,0,0)
    finally:probe.stop_grid_code_monitor_probe()
    reject(probe.stop_grid_code_monitor_probe)
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    assert monitoring.get_tool(3) is None and monitoring.get_events(3)==0
    monitoring.use_tool_id(3,'inspect cleared callbacks')
    try:
        for event in (events.PY_START,events.PY_RETURN,events.PY_UNWIND):
            assert monitoring.register_callback(3,event,None) is None
    finally:monitoring.free_tool_id(3)
    print(json.dumps({'status':'PASS_FINITE_CODE_MONITOR_LIFETIME','controls':8,
        'occupied_tool_preserved':True,'retained_callback_restored':True,'invalid_tool_rejected':True,
        'duplicate_start_and_code_rejected':True,'active_removal_and_stop_rejected':True,
        'cyclic_function_and_code_collected':True,'weak_registration_released':True,
        'tool_global_events_and_callbacks_cleared':True,'python':sys.version,
        'complete_module_thread_tool_and_lifetime_abi_accepted':False,'default_application_enabled':False}))
    return 0


if __name__=='__main__':raise SystemExit(main())
