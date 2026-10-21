"""Qualify provider positions with source opcode instrumentation enabled."""
import sys
from grid_provider_position_child import main

if __name__=='__main__':
    sys.argv.append('--traced-only')
    raise SystemExit(main())
