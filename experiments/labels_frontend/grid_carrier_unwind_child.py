"""Exercise cyclic completion owners and profiler rejection before unwrap."""
import hashlib,json,sys
from pathlib import Path
from grid_carrier_gc_child import run
from grid_comprehension_child import deny_archive
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    sources=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources']
    def fresh(policy):
        row=sources[policy=='indices'];path=ROOT/'.venv/Lib/site-packages'/row['package_path'];data=path.read_bytes()
        assert hashlib.sha256(data).hexdigest()==row['sha256']
        return next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={};path=DIRECTORY/'grid_shells.py';exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    modes=(('getitem','dense'),('getitem','single'),('getitem','complex-single'),('indices','dense'),
           ('getitem','callback-error'),('indices','callback-error'))
    rows=[]
    for reject in (False,True,'c_return'):
        for policy,mode in modes:
            try:raise LookupError('carrier observer caller')
            except LookupError as caller:
                before=run(probe,fresh(policy),shells,policy,mode,False,cycle=True,reject=reject)
                assert sys.exception() is caller
                after=run(probe,fresh(policy),shells,policy,mode,True,cycle=True,reject=reject)
                assert sys.exception() is caller
            assert after[3]==1
            rows.append({'policy':policy,'mode':mode,'profiler_rejects':reject,
                'values_and_nested_outcome_equal':before[:2]==after[:2],
                'native_cycle_release_checks':after[3],'source_cycle_release_checks':before[3],
                'weak_result_release_checks':after[2],
                'caller_handled_exception_restored':True,
                'profile_event_parity_claimed':False,'exception_chain_parity_claimed':False})
    equal=all(c['values_and_nested_outcome_equal'] for c in rows)
    print(json.dumps({'status':'PASS_FINITE_CARRIER_CYCLE_UNWIND' if equal else 'OPEN_CARRIER_CYCLE_UNWIND_GAP',
        'controls':len(rows),'python':sys.version,'cases':rows,
        'original_provider_errors_cycled_only_in_native_carrier':True,
        'complete_profile_gc_lifetime_abi_accepted':False,'default_application_enabled':False}))
    return 0 if equal else 1


if __name__=='__main__':raise SystemExit(main())
