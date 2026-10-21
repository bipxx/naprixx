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


class LineProbe:
    def __init__(self,probe): self.probe=probe

    def prepare_grid_function_probe(self,*args):
        original=args[0].__code__
        result=self.probe.prepare_grid_function_probe(*args)
        if args[5]!=1: return result
        return self.probe.prepare_grid_line_transport_probe(result,original,
            tuples(profile(original)),positions(original))

    def invoke(self,function,args):
        return self.probe.call_grid_line_transport_probe(function,args)
