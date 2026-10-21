"""Compile-only source positions for the opt-in native indices line pilot."""
import dis

from freeze_transport import profile
from native_declarations import tuples


def positions(code):
    starts=list(dis.findlinestarts(code))
    first=next(line for offset,line in starts if line>code.co_firstlineno)
    instructions=list(dis.get_instructions(code))
    def line_point(delta,last=False):
        matches=[(offset,line) for offset,line in starts if line==first+delta]
        assert matches,(delta,starts)
        return matches[-1 if last else 0]
    loop=next(item.offset for item in instructions if item.opname=='FOR_ITER')
    returned=next(item.offset for item in instructions if item.opname=='RETURN_VALUE')
    return tuple([line_point(delta) for delta in (0,1,2,3,4,6,7)] +
        [(loop,first+7),line_point(8),line_point(9),line_point(8,True),
         line_point(11),line_point(12),line_point(14),line_point(15),(returned,first+15)])


def instructions(code):
    decoded=list(dis.get_instructions(code))
    separate_truth=any(item.opname=='TO_BOOL' for item in decoded)
    simple={'LOAD_GLOBAL':'global','LOAD_FAST':'load','LOAD_FAST_CHECK':'load','STORE_FAST':'store',
        'LOAD_ATTR':'attr','LOAD_METHOD':'attr','BINARY_OP':'binary','BUILD_TUPLE':'tuple',
        'BUILD_SLICE':'build-slice','BINARY_SLICE':'slice','BINARY_SUBSCR':'item','STORE_SUBSCR':'setitem',
        'GET_ITER':'iter','FOR_ITER':'next','UNPACK_SEQUENCE':'unpack','RETURN_VALUE':'return',
        'LOAD_CONST':'const'}
    combined={'LOAD_FAST_LOAD_FAST':('load','load'),'STORE_FAST_STORE_FAST':('store','store'),
              'STORE_FAST_LOAD_FAST':('store','load')}
    rows=[]
    for item in decoded:
        name=item.opname; action=()
        if name in combined:
            names=item.argrepr.split(', ')
            assert len(names)==2,(name,item.argrepr)
            action=tuple(zip(combined[name],names))
        elif name in simple:
            kind=simple[name]
            value=str(item.argval) if kind in ('global','load','store','attr') else (
                item.argrepr if kind=='binary' else str(item.arg) if kind in ('tuple','build-slice','unpack','const') else '')
            action=((kind,value),)
        elif name=='TO_BOOL': action=(('truth',''),)
        elif name.startswith('POP_JUMP'): action=(('branch-truth' if separate_truth else 'truth',''),)
        elif name in ('CALL','CALL_KW'): action=(('call',''),)
        elif name.startswith('JUMP'): action=(('jump',''),)
        else: assert name in ('RESUME','PUSH_NULL','PRECALL','KW_NAMES','END_FOR','POP_TOP'),name
        target=item.argval if name=='FOR_ITER' or 'JUMP' in name else -1
        rows.append((item.offset,name,action,target))
    return tuple(rows)


class LineProbe:
    def __init__(self,probe,opcodes=False): self.probe,self.opcodes=probe,opcodes

    def prepare_grid_function_probe(self,*args):
        original=args[0].__code__
        result=self.probe.prepare_grid_function_probe(*args)
        if args[5]!=1: return result
        return self.probe.prepare_grid_line_transport_probe(result,original,
            tuples(profile(original)),positions(original),instructions(original) if self.opcodes else None)

    def invoke(self,function,args):
        return self.probe.call_grid_line_transport_probe(function,args)
