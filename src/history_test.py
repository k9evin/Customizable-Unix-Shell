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

assert ('1'), "Not properly display index"
assert ('ls -a'), "Not properly display command"

# exuecute the program
sendline("ls -l")
sendline("ps")
sendline("ps -j")

# execute the history builtin command
run_builtin('history')

assert ('1'), "Not properly display index"
assert ('ls -l'), "Not properly display command"
assert ('2'), "Not properly display index"
assert ('ps'), "Not properly display command"
assert ('3'), "Not properly display index"
assert ('ps -j'), "Not properly display command"

# exuecute the program
sendline("!1")

# execute the history builtin command
run_builtin('history')

assert ('1'), "Not properly display index"
assert ('ls -l'), "Not properly display command"
assert ('2'), "Not properly display index"
assert ('ps'), "Not properly display command"
assert ('3'), "Not properly display index"
assert ('ps -j'), "Not properly display command"
assert ('4'), "Not properly display index"
assert ('history'), "Not properly display command"
assert ('5'), "Not properly display index"
assert ('ls -l'), "Not properly display command"

# exuecute the program
sendline("!p")

# execute the history builtin command
run_builtin('history')

assert ('1'), "Not properly display index"
assert ('ls -l'), "Not properly display command"
assert ('2'), "Not properly display index"
assert ('ps'), "Not properly display command"
assert ('3'), "Not properly display index"
assert ('ps -j'), "Not properly display command"
assert ('4'), "Not properly display index"
assert ('history'), "Not properly display command"
assert ('5'), "Not properly display index"
assert ('ls -l'), "Not properly display command"
assert ('6'), "Not properly display index"
assert ('ps -j'), "Not properly display command"

# ensure that shell prints expected prompt
expect_prompt("The shell does not print expected prompt!")

test_success()
