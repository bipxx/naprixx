"""Trace callback failure parity and native monitor scope release."""
import hashlib,json,sys
from pathlib import Path
from grid_comprehension_child import deny_archive
from inspect_inputs import walk
import grid_observer_error_child as errors

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
    modes=(sys.argv[1],) if len(sys.argv)>1 else errors.MODES
    rows=[]
    for mode in modes:
        assert mode in errors.MODES
        before=errors.run(probe,code,shells,mode,False)
        after=errors.run(probe,code,shells,mode,True,code_monitor=True)
        counts=tuple(probe.grid_code_monitor_counts_probe())
        assert counts==(-1,0,0)
        rows.append({'mode':mode,'values_equal':before[0]==after[0],'events_equal':before[1]==after[1],
            'frame_flags_equal':before[2]==after[2],'post_context_equal':before[3]==after[3],
            'first_event_difference':next(((a,b) for a,b in zip(before[1],after[1]) if a!=b),None),
            'source_outcome':before[0],'native_outcome':after[0],
            'source_post_context':before[3],'native_post_context':after[3],'monitor_counts':counts})
    equal=all(all(c[k] for k in ('values_equal','events_equal','frame_flags_equal','post_context_equal')) for c in rows)
    print(json.dumps({'status':'PASS_FINITE_MONITOR_TRACE_ERRORS' if equal else 'OPEN_MONITOR_TRACE_ERROR_GAP',
        'controls':len(rows),'cases':rows,'python':sys.version,
        'complete_callback_tool_and_lifetime_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
