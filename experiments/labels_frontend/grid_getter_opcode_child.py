"""Run genuine getter/comprehension comparisons with opcode callbacks enabled."""
import sys
from grid_getter_line_child import main

if __name__=='__main__':
    sys.argv.append('--opcodes')
    main()
