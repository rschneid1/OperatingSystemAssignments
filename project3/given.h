#ifndef GIVEN_H
#define GIVEN_H
// contains given functions

void *start_thunk(){
	asm("popq %%rbp;\n" 		 // clean up the function prolog
	    "movq %%r13, %%rdi;\n" 	 // put arg in $rdi
	    "pushq %%r12;\n"		 // push &start_routine
	    "retq;\n"			 // return to &start_routine
	    :
	    :
	    : "%rdi"
	);
	__builtin_unreachable();
}

unsigned long int ptr_mangle(unsigned long int p){
	unsigned long int ret;
	asm("movq %1, %%rax;\n"
	    "xorq %%fs:0x30, %%rax;"
	    "rolq $0x11, %%rax;"
	    "movq %%rax, %0;"
	: "=r"(ret)
	: "r"(p)
	: "%rax"
	);
	return ret;
}

unsigned long int ptr_demangle(unsigned long int p){
	unsigned long int ret;
	asm("movq %1, %%rax;\n"
            "rorq $0x11, %%rax;"
	    "xorq %%fs:0x30, %%rax;"
	    "movq %%rax, %0;"
        : "=r"(ret)
	: "r"(p)
	: "%rax"
	);
	return ret;	
}

void pthread_exit_wrapper()
{
	unsigned long int res;
	asm("movq %%rax, %0\n":"=r"(res));
	pthread_exit((void *) res);
}


#endif
