#!/usr/bin/python
#
# Tests the functionality of history implement

import atexit, proc_check, time
from testutils import *

console = setup_tests()

# ensure that shell prints expected prompt
expect_prompt()

# sendline for testing purpose
sendline("echo hello")

# execute the history builtin command
sendline("history")

# expect "2 history" shown in console
expect("2 history")

# test !! to execute the most recent command
sendline("echo hello | rev")
sendline("!!")
expect("olleh")

sendline("ps")

# test !1 to execute the first command
sendline("!1")
expect("hello")

# test !e to execute the most recent command that starts with 'e'
sendline("ps -a")
sendline("!e")
expect("hello")

sendline("echo hello | rev")

# test nevigating commands with arrow keys
sendline("\x1B[A")
expect("olleh")

sendline("exit");

# ensure that no extra characters are output after exiting
expect_exact("exit\r\n", "Shell output extraneous characters")

test_success()
