"""Compare observable grid callable/iterator/abandoned-array finalization."""
import builtins
import ctypes
import gc
import hashlib
import json
import math
from pathlib import Path
import sys
import types
import weakref

from freeze_transport import profile
from grid_comprehension_child import deny_archive, tuples
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]
MODES=('result-type','result-type-error','indices-iterator','indices-iterator-error',
       'zip-iterator','zip-iterator-error','arange-callable')


def run(probe,codes,shells,mode,native):
    events=[]; parents={}; aranges=0

    def write(frame,**updates):
        frame.f_locals.update(updates)
        if sys.version_info<(3,13):
            sync=ctypes.pythonapi.PyFrame_LocalsToFast
            sync.argtypes,sync.restype=[ctypes.py_object,ctypes.c_int],None
            sync(frame,1)

    def normalize(value):
        if isinstance(value,Array): return ('array',tuple(value.values),value.dtype.__name__)
        if isinstance(value,(tuple,list)): return tuple(normalize(item) for item in value)
        if isinstance(value,type): return ('type',value.__name__)
        if value is None or isinstance(value,(str,int,float)): return value
        return ('object',type(value).__name__)

    def record(event,frame):
        values=frame.f_locals
        error=sys.exception()
        events.append((event,tuple(sorted(values)),normalize(values.get('typ')),
                       normalize(values.get('nn')),normalize(values.get('res')),
                       None if error is None else type(error).__name__))

    class Array:
        def __init__(self,values,dtype=int,raw=False): self.values,self.dtype,self.raw=list(values),dtype,raw
        def reshape(self,shape): return Array(self.values,self.dtype)
        def __mul__(self,value): return Array([item*value for item in self.values],self.dtype)
        def __add__(self,value): return Array([item+value for item in self.values],self.dtype)
        def __getitem__(self,key): return self
        def __del__(self):
            if self.raw and mode=='zip-iterator-error':
                try: record('array-finalize',parents['grid'])
                except BaseException as error: events.append(('finalizer-error',type(error).__name__,str(error)))

    class Callable:
        def __call__(self,*args):
            parents['grid']=sys._getframe(1)
            record('result-type-call',parents['grid'])
            if mode=='result-type-error': raise RuntimeError('result-type failure')
            return int
        def __del__(self):
            try:
                record('result-type-finalize',parents['grid'])
                if mode=='result-type': write(parents['grid'],typ=float)
            except BaseException as error: events.append(('finalizer-error',type(error).__name__,str(error)))

    class Iterator:
        def __init__(self,items,kind): self.items,self.kind,self.count=iter(items),kind,0
        def __iter__(self): return self
        def __next__(self):
            self.count+=1
            if self.kind=='indices' and mode=='indices-iterator-error' and self.count==2:
                raise RuntimeError('iterator failure')
            return next(self.items)
        def __del__(self):
            try:
                frame=parents[self.kind if self.kind=='indices' else 'grid']
                record(self.kind+'-iterator-finalize',frame)
                if self.kind=='indices' and mode=='indices-iterator': write(frame,res=('iterator-result',))
                if self.kind=='zip' and mode=='zip-iterator': write(frame,nn='iterator-result')
            except BaseException as error: events.append(('finalizer-error',type(error).__name__,str(error)))

    class Numeric:
        complexfloating=type('ComplexFloating',(),{})
        newaxis=None
        def __getattribute__(self,name):
            if name=='result_type': return Callable()
            if name=='arange' and mode=='arange-callable': return ArangeCallable()
            return object.__getattribute__(self,name)

    class ArangeCallable:
        def __call__(self,value,*,dtype):
            result=arange(value,dtype=dtype)
            self.last=weakref.ref(result)
            return result
        def __del__(self):
            try:
                result=self.last()
                holders=[item for item in gc.get_referrers(result)
                         if isinstance(item,list) and any(value is result for value in item)]
                events.append(('arange-callable-finalize',len(holders)))
                for holder in holders: holder.append(Array([99]))
            except BaseException as error: events.append(('finalizer-error',type(error).__name__,str(error)))

    def arange(value,*,dtype):
        nonlocal aranges
        aranges+=1
        if mode=='zip-iterator-error' and aranges==2: raise RuntimeError('array failure')
        return Array(range(value),dtype,True)

    def zip_callback(*args):
        parents['grid']=sys._getframe(1)
        write(parents['grid'],nn='before-comprehension')
        return Iterator(zip(*args),'zip')

    def enumerate_callback(value):
        parents['indices']=sys._getframe(1)
        return Iterator(enumerate(value),'indices')

    index_mode=mode.startswith('indices-')
    namespace={'__builtins__':vars(builtins),'__name__':'numpy._core.numeric' if index_mode else 'numpy.lib._index_tricks_impl',
               'arange':arange,'enumerate':enumerate_callback,'zip':zip_callback,'math':math}
    numeric=Numeric(); numeric.arange=arange; namespace['_nx']=numeric
    entry=1 if index_mode else 0
    code=codes[entry]; function=types.FunctionType(code,namespace)
    if native:
        shell=shells['_grid_indices' if index_mode else '_grid_getitem']
        replacement=probe.prepare_grid_function_probe(function,namespace,shell,
            tuples(profile(code)),tuples(profile(shell.__code__)),entry,
            shells['_grid_sparse_comp'],tuples(profile(shells['_grid_sparse_comp'].__code__)))
        assert isinstance(replacement,types.CodeType); function.__code__=replacement
        originals=set(walk(code))
        def guard(frame,event,arg):
            if event=='call' and frame.f_code in originals: raise AssertionError('original grid body executed')
        sys.setprofile(guard)
    try:
        try:
            value=function((2,3),int,True) if index_mode else function(types.SimpleNamespace(sparse=True),(slice(1,5,2),slice(0,3)))
            outcome=('return',normalize(value))
        except RuntimeError as error:
            outcome=('raise',type(error).__name__,str(error))
    finally:
        sys.setprofile(None)
    assert not any(row[0]=='finalizer-error' for row in events),events
    required=['indices-iterator-finalize'] if index_mode else ['result-type-finalize']
    if not index_mode and mode!='result-type-error': required.append('zip-iterator-finalize')
    if mode=='zip-iterator-error': required.append('array-finalize')
    assert all(sum(row[0]==name for row in events)==1 for name in required),events
    if mode=='arange-callable': assert sum(row[0]=='arange-callable-finalize' for row in events)==2,events
    return outcome,events


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    pins=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())
    codes=[]
    for row in pins['sources']:
        path=ROOT/'.venv/Lib/site-packages'/row['package_path']; data=path.read_bytes()
        assert hashlib.sha256(data).hexdigest()==row['sha256']
        codes.append(next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname']))
    shells={}; path=DIRECTORY/'grid_shells.py'
    exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    for mode in MODES:
        before=run(probe,codes,shells,mode,False)
        after=run(probe,codes,shells,mode,True)
        assert before==after,(mode,before,after)
    print(json.dumps({'status':'PASS','python':sys.version,'controls':len(MODES),'original_grid_execution_in_native_run':False}))


if __name__=='__main__': main()
