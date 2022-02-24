Student Information
-------------------
Name:Jiayue Lin, PID:jiayuelin
Name:Mingkai Pang, PID:pangmin

How to execute the shell
------------------------
In the src/ directory:
First, run the "make" command from Makefile to compile the program.
Second, execute the "./cush" command to run the executable shell.

Important Notes
---------------
The shell we implemented passed all basic and advanced tests. There 
is no important notes that require attention.

Description of Base Functionality
---------------------------------
<describe your IMPLEMENTATION of the following commands:
kill, stop, \ˆC, \ˆZ >

jobs: The job command iterates through the list of jobs and then 
prints out all the jobs that are both running and stopped in the 
background. It will first check to see if there is only one
argument.
An example of executing the jobs command is shown below:
cush> sleep 100 &
[1] 1271483
cush> jobs
[1]     Running         (sleep 100)
Note, "[1]" is the jid, "Running" is the current status of the job,
"(sleep 100)" is the name of the job.

fg: The fg command brings running and stopped job back from the background
into the foreground. If there is one argument, the shell retrieves the most 
recent running or stopped the job and modifies its status from background 
to foreground. The shell will then gives terminal the control of that job and
send a SIGCONT signal if the job is stopped. If there are two arguments, 
the shell brings the specified running and stopped jobs and changes their status
from background to foreground. The shell will then gives the terminal control 
of these jobs and send a SIGCONT command if the job is stopped.
An example of executing the fg command is shown below:
cush> fg 1
sleep 100
Note, "1" is the id of the job that is getting retrieved. "sleep 100" is the
name of the job.

bg: The bg command continues the stopped job in the foreground and sends it to 
the background. If there is one argumement, the shell retrieves the most recent
stopped job and modifies its status from foreground to background. The shell
will then gives terminal the control of that job and a send a SIGCONT signal if
the job is stopped. If there are two arguments, the shell brings the specfied stopped
jobs and changed their status to running. The shell will then gives terminal 
the control of these jobs and a send a SIGCONT signal if the job is stopped.
An example of executing the bg command is shown below:
cush> sleep 100 &
[1] 1290616
cush> jobs
[1]     Running         (sleep 100)
cush> fg 1
sleep 100
^Z[1]   Stopped         (sleep 100)
cush> bg 1
cush> jobs
[1]     Running         (sleep 100)
Note, "[1]" is the jid, "Running" is the current status of the job,
"(sleep 100)" is the name of the job.

kill: The kill command kills a specified job by its jid. It retrieves the specified
job and sents a SUGKILL signal through killpg() method.

Description of Extended Functionality
-------------------------------------
<describe your IMPLEMENTATION of the following functionality:
I/O, Pipes, Exclusive Access >
List of Additional Builtins Implemented
---------------------------------------
(Written by Your Team)
<builtin name>
<description>

