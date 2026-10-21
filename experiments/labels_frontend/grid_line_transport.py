"""Compile-only source positions for the opt-in native indices line pilot."""
import dis
import sys

from freeze_transport import profile
from native_declarations import tuples


def getter_positions(code):
    starts=list(dis.findlinestarts(code)); decoded=list(dis.get_instructions(code))
    byline={}
    for offset,line in starts: byline.setdefault(line,[]).append(offset)
    placeholder=(byline[147][0],147)
    points=[(byline[line][0],line) if line in byline else placeholder for line in range(146,209)]
    points.extend([placeholder]*(89-len(points)))
    loops=[item for item in decoded if item.opname=='FOR_ITER']
    inline=len(loops)==4
    for index,item,line in ((64,loops[0],152),(65,loops[-2],173),(66,loops[-1],187)):
        points[index]=(item.offset,line)
    # Parent CALL line for 3.11 and post-argument append line are repeated lines.
    points[71]=(byline[169][-1],169)
    points[72]=(byline[164][-1],164)
    if inline:
        loop=loops[1]; position=decoded.index(loop); first=position
        if first and decoded[first-1].opname=='GET_ITER': first-=1
        while first and decoded[first-1].opname=='EXTENDED_ARG': first-=1
        points[67]=(decoded[first].offset,170); points[68]=(loop.offset,170)
        body=next(offset for offset in byline[169] if offset>loop.offset)
        points[69]=(body,169)
        store=next(item for item in decoded if item.offset>loop.offset and item.opname.startswith('STORE_FAST') and
                   'nn' in item.argrepr.split(', '))
        points[70]=(store.offset,169)
    for index,line in ((73,191),(74,192),(75,206),(76,208)):
        candidates=[item.offset for item in decoded if item.opname=='RETURN_VALUE' and item.positions.lineno==line]
        assert len(candidates)==1,(line,candidates)
        points[index]=(candidates[0],line)
    calls=[item for item in decoded if item.opname in ('CALL','CALL_KW')]
    for index,line,which in ((77,152,0),(78,152,1),(79,208,-1),(80,206,-1)):
        candidates=[item for item in calls if item.positions.lineno==line]
        points[index]=(candidates[which].offset,line)
        # In 3.11/3.12, an inlined Python call advances over its caches before
        # entering the callee. An exception propagated from it retains that IP.
        cached=list(dis.get_instructions(code,show_caches=True))
        offset=candidates[which].offset
        position=next(i for i,item in enumerate(cached) if item.offset==offset)+1
        while position<len(cached) and cached[position].opname=='CACHE':
            offset=cached[position].offset;position+=1
        points[index+4]=(offset if sys.version_info<(3,13) else candidates[which].offset,line)
    reraised=[item for item in decoded if item.opname=='RERAISE' and item.arg==0 and item.positions.lineno==193]
    assert len(reraised)==1,reraised
    points[85]=(reraised[0].offset,193)
    for index,name in ((86,'IndexError'),(87,'TypeError')):
        matches=[item for item in decoded if item.opname=='LOAD_GLOBAL' and item.argval==name and item.positions.lineno==193]
        assert len(matches)==1,matches
        points[index]=(matches[0].offset,193)
    matched=[item for item in decoded if item.opname=='CHECK_EXC_MATCH']
    assert len(matched)==1,matched
    points[88]=(matched[0].offset,193)
    return tuple(points)


def comprehension_positions(code):
    decoded=list(dis.get_instructions(code)); first=code.co_firstlineno
    unpack=next(item.offset for item in decoded if item.opname=='UNPACK_SEQUENCE')
    body=next(item.offset for item in decoded if item.opname=='LOAD_GLOBAL')
    loop=next(item.offset for item in decoded if item.opname=='FOR_ITER')
    returned=next(item.offset for item in decoded if item.opname=='RETURN_VALUE')
    return ((2,first),(unpack,first+1),(body,first),(loop,first),(returned,first))


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
        'LOAD_CONST':'const','BUILD_LIST':'list','LIST_APPEND':'append','CALL_FUNCTION_EX':'starcall',
        'COMPARE_OP':'compare','LOAD_FAST_AND_CLEAR':'take','MAKE_FUNCTION':'function'}
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
            value=str(item.argval) if kind in ('global','load','store','attr','take','compare') else (
                item.argrepr if kind=='binary' else str(item.arg) if kind in ('tuple','list','build-slice','unpack','const') else '')
            action=((kind,value),)
        elif name=='TO_BOOL': action=(('truth',''),)
        elif name.startswith('POP_JUMP') and name.endswith('NOT_NONE'): action=(('not-none',''),)
        elif name.startswith('POP_JUMP'): action=(('branch-truth' if separate_truth else 'truth',''),)
        elif name in ('CALL','CALL_KW'): action=(('call',''),)
        elif name.startswith('JUMP'): action=(('jump',''),)
        else: assert name in ('RESUME','PUSH_NULL','PRECALL','KW_NAMES','END_FOR','POP_TOP',
                              'NOP','SWAP','COPY','PUSH_EXC_INFO','POP_EXCEPT','CHECK_EXC_MATCH','RERAISE'),name
        target=item.argval if name=='FOR_ITER' or 'JUMP' in name else -1
        rows.append((item.offset,name,action,target))
    return tuple(rows)


class LineProbe:
    def __init__(self,probe,opcodes=False): self.probe,self.opcodes=probe,opcodes

    def prepare_grid_function_probe(self,*args):
        original=args[0].__code__
        result=self.probe.prepare_grid_function_probe(*args)
        if args[5]==0:
            children=[item for item in original.co_consts if hasattr(item,'co_code')]
            if children:
                child=children[0]; payload=result.co_consts[-1]
                converted=self.probe.prepare_grid_line_transport_probe(payload[2],child,
                    tuples(profile(child)),comprehension_positions(child),instructions(child) if self.opcodes else None)
                result=result.replace(co_consts=(*result.co_consts[:-1],(*payload[:2],converted)))
            return self.probe.prepare_grid_line_transport_probe(result,original,
                tuples(profile(original)),getter_positions(original),instructions(original) if self.opcodes else None)
        if args[5]!=1: return result
        return self.probe.prepare_grid_line_transport_probe(result,original,
            tuples(profile(original)),positions(original),instructions(original) if self.opcodes else None)

    def invoke(self,function,args):
        return self.probe.call_grid_line_transport_probe(function,args)
