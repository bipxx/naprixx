"""Run the reentry contracts with direct per-code entry for the outer frame."""
import contextlib,io,json,sys
import grid_reentry_child as reentry


def main():
    import _frontend_policy_probe as probe
    if sys.version_info<(3,12):
        print(json.dumps({'status':'OPEN_CP311_CODE_MONITOR','controls':0}));return 0
    original=reentry.run
    def monitored(*args,**kwargs):
        kwargs['code_monitor']=True
        return original(*args,**kwargs)
    output=io.StringIO();reentry.run=monitored
    try:
        with contextlib.redirect_stdout(output):result=reentry.main()
    finally:reentry.run=original
    assert tuple(probe.grid_code_monitor_counts_probe())==(-1,0,0)
    data=json.loads(output.getvalue())
    data.update({'entry_protocol':'explicit_per_code_monitor','outer_invocation_adapter':False,
        'tool_and_registrations_released':True,'complete_monitor_thread_and_reentry_abi_accepted':False})
    print(json.dumps(data));return result


if __name__=='__main__':raise SystemExit(main())
