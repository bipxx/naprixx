"""Audit-hook reentry and retry after a partially cleared callback set."""
import json,sys
from grid_comprehension_child import deny_archive


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        print(json.dumps({'status':'OPEN_CP311_CODE_MONITOR','controls':0}));return 0
    monitor=sys.monitoring;control={};rows=[]
    def audit(event,args):
        if event!='sys.monitoring.register_callback' or not control.get('armed'):return
        control['calls']+=1
        if control['kind']=='rollback':
            if control['calls'] not in (control['at'],control['at']+1):return
            if control['calls']==control['at']+1:control['armed']=False
            raise RuntimeError('start audit veto' if control['calls']==control['at'] else 'rollback audit veto')
        if control['calls']!=control['at']:return
        control['armed']=False
        if control['kind']=='veto':raise RuntimeError('audit veto')
        try:
            if control['action']=='stop':probe.stop_grid_code_monitor_probe()
            elif control['action']=='start':probe.start_grid_code_monitor_probe(4)
            else:probe.set_grid_code_monitor_probe((lambda:None).__code__,True)
        except ValueError as error:control['reentry_error']=str(error)
        else:control['reentry_error']='recursive operation succeeded'
    sys.addaudithook(audit)
    configs=[('reentry',phase,action,at) for phase in ('start','stop') for action in ('start','stop','set') for at in (1,2)]
    configs += [('veto','start',None,at) for at in (1,2,3)]
    configs += [('veto','stop',None,at) for at in range(1,7)]
    configs += [('rollback','start',None,2)]
    if '--stop-reentry' in sys.argv:configs=[('reentry','stop','stop',1)]
    if '--partial-veto' in sys.argv:configs=[('veto','stop',None,5)]
    if '--rollback-veto' in sys.argv:configs=[('rollback','start',None,2)]
    for kind,phase,action,at in configs:
        assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
        if phase=='stop':probe.start_grid_code_monitor_probe(3)
        control.clear();control.update(armed=True,calls=0,at=at,kind=kind,action=action)
        failure=None
        try:
            if phase=='start':probe.start_grid_code_monitor_probe(3)
            else:probe.stop_grid_code_monitor_probe()
        except BaseException as error:failure=(type(error).__name__,str(error))
        control['armed']=False
        if kind=='reentry':
            equal=failure is None and control.get('reentry_error')=='code monitor transition is active'
            if phase=='start' and failure is None:probe.stop_grid_code_monitor_probe()
        else:
            equal=failure==('RuntimeError','rollback audit veto' if kind=='rollback' else 'audit veto')
            try:
                if kind=='veto' and phase=='start':probe.start_grid_code_monitor_probe(3)
                probe.stop_grid_code_monitor_probe()
            except BaseException as error:equal=False;failure=(failure or ())+(type(error).__name__,str(error))
        released=tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0) and monitor.get_tool(3) is None and monitor.get_events(3)==0
        rows.append({'kind':kind,'phase':phase,'action':action,'at':at,'equal':equal,'released':released,
            'failure':failure,'reentry_error':control.get('reentry_error')})
        if not equal or not released:
            print(json.dumps({'status':'OPEN_AUDIT_TRANSITION','controls':len(rows),'cases':rows}));return 1
        monitor.use_tool_id(3,'audit cleanup inspection')
        try:
            for event in (monitor.events.PY_START,monitor.events.PY_RETURN,monitor.events.PY_UNWIND):
                assert monitor.register_callback(3,event,None) is None
        finally:monitor.free_tool_id(3)
    print(json.dumps({'status':'PASS_FINITE_AUDIT_TRANSITIONS','controls':len(rows),'cases':rows,
        'callbacks_events_reservation_released':True,'complete_audit_shutdown_abi_accepted':False,'python':sys.version}));return 0


if __name__=='__main__':raise SystemExit(main())
