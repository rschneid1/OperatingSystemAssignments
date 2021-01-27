Project 4 Readme
Ryan Schneider
*
*

Implemenation:
	This library implements thread local storage. Implemented tls_create, tls_read, tls_destroy,tls_write, tls_clone. Followed the guidelines in the Homework 4 Discussion. Instead of using hashing, decided to use a method similar to my project3 implemenation, where I instantiated every hash table tid to a default value (in this case to a negative number (-1), as pthread_t tid is non-negative when generated). Then just used iteration and comparing default value to find open slots. Used the calculation in the cited source (geeksforgeeks) for finding the ceil without using math lib.

Problems: No major problems during development. Kept having fork errors from bandit, so had to relog out and back in a couple times.

Resources:
https://www.geeksforgeeks.org/find-ceil-ab-without-using-ceil-function/
