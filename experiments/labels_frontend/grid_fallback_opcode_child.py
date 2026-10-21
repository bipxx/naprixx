"""Compare fallback and exception paths with source opcode callbacks enabled."""
import sys
from grid_fallback_child import main

if __name__=='__main__':
    sys.argv.append('--opcodes')
    main()
