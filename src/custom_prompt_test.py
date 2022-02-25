#!/usr/bin/python
#
# Tests the functionality of custom prompt implement

import atexit, proc_check, time
from testutils import *

console = setup_tests()

# ensure that shell prints expected prompt
#setup
sendline("")

# ensure that shell prints expected prompt
expect("<[^@]*@[^>]*>\$")

test_success()