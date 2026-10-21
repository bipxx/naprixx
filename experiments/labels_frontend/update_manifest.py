"""Refresh the explicit experiment-byte inventory after reviewed source edits.

This does not change the pinned application source inputs. A mismatch there
requires a new selection/review rather than refreshing the input hashes here.
"""
import hashlib
import json
from pathlib import Path

DIRECTORY=Path(__file__).resolve().parent
ROOT=DIRECTORY.parents[1]


if __name__=='__main__':
    path=DIRECTORY/'manifest.json'
    data=json.loads(path.read_text())
    for name,expected in data['source_inputs'].items():
        assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==expected,name
    data['experiment_inputs']={path.name:hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(DIRECTORY.iterdir()) if path.is_file() and path.name!='manifest.json'}
    data['experimental_declaration_capture_implemented']=True
    data['experimental_native_first_declarations_implemented']=True
    data['experimental_numpy_grid_policies_implemented']=True
    data['default_application_producer_capture_enabled']=False
    data['atomic_publication_implemented']=True
    data['atomic_publication_scope']='Experimental GIL-held batch with notification-before-commit protocol; no default startup enablement or complete compatibility approval'
    path.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8',newline='\n')
