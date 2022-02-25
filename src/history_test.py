#!/usr/bin/python
#
# Tests the functionality of history implement

import atexit, proc_check, time
from testutils import *

console = setup_tests()

# ensure that shell prints expected prompt
expect_prompt()

# exuecute the program
sendline("ls -a")

# execute the history builtin command
run_builtin('history')

# ensure that shell prints expected prompt
expect_prompt("The shell does not print expected prompt!")

test_success()
