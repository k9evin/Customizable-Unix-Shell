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
bg, kill, stop, \ˆC, \ˆZ >

jobs: The job command iterates through the list of jobs and then 
prints out all the jobs that are both running and stopped in the 
background. It will first check to see if there is only one
argument.
An example of executing the jobs command is shown below:
cush>$ jobs
[2]+  Done                    sleep 3
Note, "[2]+" is the jid, "Done" is the current status of the job,
"sleep 3" is the name of the job.

fg: The fg command brings running and stopped job back from the background
into the foreground. If there is one argument, the shell retrieves the most 
recent running or stopped the job and modifies its status from background 
to foreground. If there are two arguments, the shell brings the specified 
running and stopped jobs and changes its status from background to foreground.
An example of executing the fg command is shown below:
cush>$ sleep 100 &
[1] 1254602
cush>$ fg 1
sleep 100
Note, "1" is the id of the job that is getting retrieved.

bg: The bg command sends a 
 

Description of Extended Functionality
-------------------------------------
<describe your IMPLEMENTATION of the following functionality:
I/O, Pipes, Exclusive Access >
List of Additional Builtins Implemented
---------------------------------------
(Written by Your Team)
<builtin name>
<description>

