"""Direct-call trace diagnostic under an explicitly installed evaluator scope."""
import contextlib
import io
import json
import sys
from grid_comprehension_child import deny_archive
import grid_retained_frame_child as retained


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    def reject(action):
        try:action()
        except ValueError:return
        raise AssertionError('unsafe evaluator transition admitted')
    probe.set_grid_eval_scope_probe(True)
    try:
        reject(lambda:probe.set_grid_eval_scope_probe(True))
        reject(lambda:probe.set_grid_eval_scope_probe(False))
    finally:probe.set_grid_eval_scope_probe(False)
    reject(lambda:probe.set_grid_eval_scope_probe(False))
    output=io.StringIO()
    sys.argv.extend(['--completion-transport','--direct-call','--direct-traces'])
    same_setup='--source-without-hook' not in sys.argv
    if same_setup:
        probe.set_grid_eval_scope_probe(True)
        try:
            with contextlib.redirect_stdout(output):result=retained.main()
        finally:probe.set_grid_eval_scope_probe(False)
    else:
        original=retained.run
        def candidate(*args,**kwargs):
            if not args[4]:return original(*args,**kwargs)
            probe.set_grid_eval_scope_probe(True)
            try:return original(*args,**kwargs)
            finally:probe.set_grid_eval_scope_probe(False)
        retained.run=candidate
        try:
            with contextlib.redirect_stdout(output):result=retained.main()
        finally:retained.run=original
    data=json.loads(output.getvalue())
    data.update({'evaluator_scope_explicitly_enabled':True,'source_and_native_use_same_evaluator_hook':same_setup,
        'duplicate_enable_active_disable_and_duplicate_disable_rejected':True,
        'unselected_frame_and_hook_composition_abi_accepted':False,'default_application_enabled':False})
    print(json.dumps(data))
    return result


if __name__=='__main__':raise SystemExit(main())
