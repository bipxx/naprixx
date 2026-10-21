"""Opt-in native opcode/source comparisons; no original policy execution."""
import hashlib
import json
from pathlib import Path
import sys

from grid_comprehension_child import deny_archive
from grid_line_child import MODES,run
from inspect_inputs import walk

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


def main():
    sys.addaudithook(deny_archive)
    import _frontend_policy_probe as probe
    row=json.loads((DIRECTORY/'grid_provider_controls.json').read_text())['sources'][1]
    path=ROOT/'.venv/Lib/site-packages'/row['package_path']; data=path.read_bytes()
    assert hashlib.sha256(data).hexdigest()==row['sha256']
    code=next(item for item in walk(compile(data,str(path),'exec',dont_inherit=True)) if item.co_qualname==row['qualname'])
    shells={}; path=DIRECTORY/'grid_shells.py'
    exec(compile(path.read_bytes(),str(path),'exec',dont_inherit=True),shells)
    modes=(*MODES,'opcode-load','opcode-call','opcode-store')
    if sys.version_info>=(3,13): modes+=('opcode-fused-load',)
    for mode in modes:
        try:
            before=run(probe,code,shells['_grid_indices'],mode,False,opcodes=True)
            after=run(probe,code,shells['_grid_indices'],mode,True,opcodes=True)
        except BaseException:
            print('CONTROL_FAILED',mode,flush=True)
            raise
        assert before==after,(mode,before,after)
    print(json.dumps({'status':'PASS_FINITE_NATIVE_INDICES_OPCODES','controls':len(modes),'python':sys.version,
        'jump_and_complete_frame_parity_accepted':False,'default_application_enabled':False}))


if __name__=='__main__': main()
