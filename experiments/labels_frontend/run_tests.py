"""Run the focused experiment with access to the ignored evidence archive denied."""
import os
from pathlib import Path
import sys

DIRECTORY=Path(__file__).resolve().parent
ARCHIVE=os.path.normcase(str(DIRECTORY.parents[1]/'.artifacts'))


def deny_archive(event,args):
    if event not in ('open','os.listdir','os.scandir') or not args:
        return
    value=args[0]
    if not isinstance(value,(str,bytes,os.PathLike)):
        return
    path=os.path.normcase(os.path.abspath(os.fsdecode(value)))
    if path==ARCHIVE or path.startswith(ARCHIVE+os.sep):
        raise RuntimeError('The portable frontend experiment must not access .artifacts: '+path)


if __name__=='__main__':
    sys.addaudithook(deny_archive)
    import pytest
    raise SystemExit(pytest.main([str(DIRECTORY),'-p','no:cacheprovider',
        '-W','error::pytest.PytestUnraisableExceptionWarning',*sys.argv[1:]]))
