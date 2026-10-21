"""Dependency-free process-isolated publication/audit/watcher controls."""
import json
import sys
import threading
import weakref
import gc

from run_tests import deny_archive
sys.addaudithook(deny_archive)
import _frontend_policy_probe as probe


def main():
    namespace={}
    exec('def f(x): return x+1\ndef g(x): return x+2\n',namespace)
    functions=namespace['f'],namespace['g']
    replacement={}
    exec('def f(x): return x+11\ndef g(x): return x+22\n',replacement)
    codes=replacement['f'].__code__,replacement['g'].__code__
    before=tuple(function.__code__ for function in functions)
    aliases=tuple(functions)
    def old():
        return all(function.__code__ is code for function,code in zip(functions,before,strict=True))
    def new():
        return all(function.__code__ is code for function,code in zip(functions,codes,strict=True))
    def publish():
        probe.publish_code_batch_probe(functions,codes)
    scenario=sys.argv[1]
    if scenario=='success-cache':
        def call(): return functions[0](5),functions[1](5)
        for _ in range(20000): assert call()==(6,7)
        versions=tuple(probe.function_version_probe(fn) for fn in functions)
        publish()
        assert new() and all(probe.function_version_probe(function)==0 for function in functions)
        if hasattr(probe,'version_cache_probe'):
            for fn,version in zip(functions,versions,strict=True):
                if version:
                    assert probe.version_cache_probe(version)[0] is not fn
        for _ in range(100): assert call()==(16,27)
        assert all(left is right for left,right in zip(aliases,functions,strict=True))
    elif scenario.startswith('audit-'):
        observed=[]
        def audit(event,args):
            if event!='object.__setattr__' or args[1]!='__code__' or args[0] not in functions:
                return
            observed.append(args[0])
            assert old()
            if scenario=='audit-reject' and len(observed)==2:
                raise ValueError('reject second before any writes')
            if scenario=='audit-reentry':
                try: publish()
                except RuntimeError as error: assert 'already active' in str(error)
                else: raise AssertionError('reentrant publisher accepted')
            if scenario=='audit-other-thread':
                outcomes=[]
                def worker():
                    try: publish()
                    except RuntimeError: outcomes.append('blocked')
                thread=threading.Thread(target=worker)
                thread.start();thread.join(timeout=5)
                assert not thread.is_alive() and outcomes==['blocked']
        sys.addaudithook(audit)
        if scenario=='audit-reject':
            try: publish()
            except ValueError: pass
            else: raise AssertionError('audit rejection ignored')
            assert old()
        else:
            publish();assert new()
        assert len(observed)==2
    elif scenario.startswith('watcher-'):
        if not hasattr(probe,'start_watcher_probe'):
            print(json.dumps(dict(scenario=scenario,status='NOT_APPLICABLE_CP311')));return
        observed=[]
        unraisable=[]
        sys.unraisablehook=lambda args:unraisable.append((args.exc_type,args.err_msg))
        def watcher(function,code):
            if function not in functions: return
            observed.append(function)
            assert old()
            if scenario=='watcher-error': raise ValueError('watcher observer failure')
            if scenario=='watcher-mutate-default':
                function.__defaults__=(4,)
        probe.start_watcher_probe(watcher)
        try:
            if scenario=='watcher-mutate-default':
                try: publish()
                except TypeError: pass
                else: raise AssertionError('foreign defaults mutation ignored')
                assert old() and functions[0].__defaults__==(4,)
            else:
                publish();assert new()
                assert len(observed)==2
                assert len(unraisable)==(2 if scenario=='watcher-error' else 0)
        finally:
            probe.stop_watcher_probe()
    elif scenario=='foreign-code-change':
        fired=[]
        def audit(event,args):
            if event=='object.__setattr__' and args[0] is functions[0] and args[1]=='__code__' and not fired:
                fired.append(True)
                functions[1].__code__=codes[1]
        sys.addaudithook(audit)
        try: publish()
        except TypeError: pass
        else: raise AssertionError('foreign code change ignored')
        assert functions[0].__code__ is before[0] and functions[1].__code__ is codes[1]
    elif scenario=='duplicate':
        try: probe.publish_code_batch_probe((functions[0],functions[0]),codes)
        except TypeError: pass
        else: raise AssertionError('duplicate accepted')
        assert old()
    else:
        raise AssertionError(scenario)
    print(json.dumps(dict(scenario=scenario,status='PASS')))


if __name__=='__main__':
    main()
