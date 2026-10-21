"""Run the same caller contracts with a per-code monitoring scope."""
import contextlib,io,json,sys
import grid_function_caller_child as caller


def main():
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        try:probe.start_grid_code_monitor_probe(3)
        except NotImplementedError:pass
        else:raise AssertionError('unsupported monitor admitted')
        print(json.dumps({'status':'OPEN_CP311_CODE_MONITOR','controls':0,'python':sys.version}));return 0
    original=caller.run
    def monitored(*args,**kwargs):
        kwargs['code_monitor']=bool(args[6])
        return original(*args,**kwargs)
    output=io.StringIO();caller.run=monitored
    try:
        with contextlib.redirect_stdout(output):result=caller.main()
    finally:caller.run=original
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    assert sys.monitoring.get_tool(3) is None and sys.monitoring.get_events(3)==0
    data=json.loads(output.getvalue())
    data.update({'entry_protocol':'explicit_per_code_monitor','function_vectorcall_changed':False,
        'interpreter_evaluator_changed':False,'tool_and_registrations_released':True,
        'complete_monitoring_resource_and_observer_abi_accepted':False})
    print(json.dumps(data));return result


if __name__=='__main__':raise SystemExit(main())
