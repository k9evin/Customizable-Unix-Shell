#!/usr/bin/python
#
# Tests the functionality of custom prompt implement
# Also serves as example of how to write your own
# custom functionality tests.
#
import atexit, proc_check, time
from testutils import *

console = setup_tests()

# ensure that shell prints expected prompt
#setup
sendline("")
expect_prompt("<[^@]*@[^>]*>\$")
test_success()