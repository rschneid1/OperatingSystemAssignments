Author: Ryan Schneider
BUID: *
Email: *

Explanation:

	Created several global variables to hold the schedule and the Thread Control Block. Then created an init function that initialized the default values in these global variables. Then using the techniques described in the HW pdf, assigned the correct values and registers for each TCB (Thread Context). Then designed the scheduler according to the Round Robin schedule.

Because I am using an array, I had to keep track of the amount of threads on the schedule , and iterate over the schedule to avoid duplicates.

Resources:
	https://www.gnu.org/software/libc/manual/html_node/Sigaction-Function-Example.html

Issues:
	No Issues With Bandit
