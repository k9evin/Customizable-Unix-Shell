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
job and sends a SIGKILL signal through killpg() method. The shell saves the state of
specified job in the terminal.
An example of executing the kill command is shown below:
cush> sleep 100 &
[1] 1275702
cush> kill 1
cush> killed
Success
Note, "1" is the specified jid that is getting killed.

stop: The stop command stops a specified job by its jid. It retrieves the specified job
and sends a SIGSTOP through the killpg() method. The shell saves the state of
the specified job in the terminal.
An example of executing the stop command is shown below:
cush> sleep 100 &
[1] 1278308
cush> stop 1
cush> [1]       Stopped         (sleep 100)
Note, "1" is the specified jid that is stopped.

\ˆC: The Ctrl-C command sends a WIFSIGNALED signal to kill the process group in the foreground.
Similar to the kill command.
An example of executing the Ctrl-C command is shown below:
cush> sleep 100 &
[1] 1286810
cush> ^C

\ˆZ: The Ctrl-Z command sends a WIFSTOPPED signal to stop the process group in the foreground.
Similar to the stop command. The shell then prints out the jid, job status, and the name of the job.
An example of executing the Ctrl-Z command is shown below:
^Z[1]   Stopped         (sleep 100)

Description of Extended Functionality
-------------------------------------
I/O: The I/O redirections read input from the iored_input file and write the last command to the 
iored_output file. It then check if needs to be appended to the end of the file. Else, write the
last command to the iored_output file. Last but not least, it checks to see if stderr should be 
redirected as well.

Pipes: The I/O Piping iterates through the commands in the pipeline and makes sure to block
the signal while doing so. Put the commands into an array, where the array of pointers to 
words makes up the command. It forks off a child process to execute each command in a pipeline. 
Creates a new process group if this is the first command in the pipe. Else, for the other commands
in the pipe, put them in the group of the first command. For the parent code block, sets the
process group id to the first spawned processes pid. Fills in the pid array in jobs and then 
increment counter as well as update the job. Again, increment the counter. Last but not least, 
call function to close all open pipes.

Exclusive Access: The exclusive access updates the job status and print job. Give control of
the terminal to the running process group. Wait for the job to finish. Ater waiting completed return 
back terminal control to the shell. Last but not least, unblock the SigChld.

List of Additional Builtins Implemented
---------------------------------------
custom prompt
The custom prompt outputting username, hostname, and the current working directory.

history
The history builtin prints out the user's history, up/down arrow key navigation for previous commands.
!substring runs the most recent command starting with substring. !! runs the most recent command. !n
runs the nth command in the history.