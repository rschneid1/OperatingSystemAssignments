#include <pthread.h>
#include <unistd.h> // ualarm
#include <stdlib.h> // malloc
#include <stdio.h> // printf
#include <signal.h>
#include <time.h>
#include <setjmp.h>
#include <sys/types.h>
#include <string.h>
#include "given.h"
// Static Globals
#define MAX_THREAD_COUNT 128
#define STACK_SIZE 32767
#define QUANTUM 50
#define JB_RBX 0
#define JB_RBP 1
#define JB_R12 2
#define JB_R13 3
#define JB_R14 4
#define JB_R15 5
#define JB_RSP 6
#define JB_PC 7

// * * * Globals * * * 
struct TCB_entry {
	int thread_id;
	int thread_status;
	void *stack_pointer;
	//unsigned long int* stack_exit;
	jmp_buf buffer;
};

// create TCB Structure
struct TCB_entry TCB[MAX_THREAD_COUNT];
int schedule[MAX_THREAD_COUNT];

int thread_count = 0;
// current_thread is always 0
int current_thread = 0;
int thread_index = 0;
int schedule_last = 0;
int init_flag = 0;

timer_t timerid;

struct sigaction new_action;
// * * * * * * * * * * 


pthread_t pthread_self(){
	return (pthread_t) schedule[0];
}

/* Thread Status
* 0 : thread is ready
* 1 : thread is running
* 2 : thread is paused
* 3 : thread ran until completion
* 99 : open TCB slot 
*/


// places current thread to back of TCB , and shifts rest of entries to the left

void cycle(){
	int i;
	int temp = schedule[0];
	for(i = 0; i < schedule_last - 1; i++){
		schedule[i] = schedule[i + 1];
	}
	schedule[schedule_last - 1] = temp;
}

void shift(){
	int i;
	for(i = 0; i < schedule_last;i++){
		schedule[i] = schedule[i + 1];
	}
	schedule_last--;
}

// loop through TCB adding ready processes to schedule
void update(){
	int i;
	for(i = 0; i < MAX_THREAD_COUNT;i++){
		if(TCB[i].thread_status == 3){
			shift();
			TCB[i].thread_status = 99;
		}		
	}	
}

void scheduler(){
	// round robin schedule
	if (setjmp(TCB[current_thread].buffer) == 0){
		// make sure schedule not accidentally firing after exit
		if(TCB[current_thread].thread_status != 99){
			TCB[current_thread].thread_status = 2; 
		}
		update();
		cycle();
		current_thread = schedule[0];
		TCB[current_thread].thread_status = 1;
		longjmp(TCB[current_thread].buffer, 1);	
	}
}

int system_init(){
	//printf("Init TCB . . .\n");
	// init TCB
	int i;
	for(i = 0;i < MAX_THREAD_COUNT;i++){
		TCB[i].thread_status = 99;
		//schedule[i] = -1;
	}
	
	// assign TCB for main
	TCB[0].thread_id = 0;
	TCB[0].thread_status = 0;
	TCB[0].stack_pointer = malloc(STACK_SIZE);	
	if(TCB[0].stack_pointer == NULL){
		printf("Could not allocate memory ... \n");
		exit(0);
	}			
	
	// set up exit function
	//unsigned long int *stack_top = (unsigned long int*)TCB[0].stack_pointer + ((STACK_SIZE/sizeof(unsigned long int)) - 1);	
	//*stack_top = (unsigned long int) pthread_exit;

	//TCB[0].buffer[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long int) stack_top);
	//TCB[0].buffer[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int)start_thunk); 
	//TCB[0].buffer[0].__jmpbuf[JB_R12] = (unsigned long int)start_routine;
	//TCB[0].buffer[0].__jmpbuf[JB_R13] = (unsigned long int) arg; 

	setjmp(TCB[0].buffer);		


	// set up signal handler
	new_action.sa_handler = scheduler;
	new_action.sa_flags = SA_NODEFER;
	sigaction(SIGALRM,&new_action,NULL);				
	
	// set scheduler in motion
	//useconds_t q = QUANTUM;	
	ualarm(QUANTUM * 1000,QUANTUM * 1000);		
	
	thread_count++;
	schedule_last++;
	current_thread = 0;
	return 1;
}

int pthread_create(
	pthread_t *thread,
	const pthread_attr_t *attr,
	void *(*start_routine) (void *),
	void *arg
){
	
	// fail if thread_count too high
	if(thread_count >= MAX_THREAD_COUNT){
		printf("Reached Thread Limit...\n");
		return -1;
	}


	// create subsystem
	if(thread_count == 0 && init_flag == 0){
		
		init_flag = 1;
		system_init();
	}
	
	// find empty TCB entry	
	int i;
	int index = 0;
	for(i = 1; i < MAX_THREAD_COUNT; i++){
		if (TCB[i].thread_status == 99){
			index = i;
			break;
		}
	}
				
	// assign TCB
	TCB[index].thread_id = *thread;
	TCB[index].thread_status = 0;
	TCB[index].stack_pointer = malloc(STACK_SIZE);	
	if(TCB[index].stack_pointer == NULL){
		printf("Could not allocate memory ... \n");
		exit(0);
	}			
	
	// set up exit function
	unsigned long int *stack_top = (unsigned long int*)TCB[index].stack_pointer + ((STACK_SIZE/sizeof(unsigned long int)) - 1);	
	*stack_top = (unsigned long int) pthread_exit;
	
	setjmp(TCB[index].buffer);	
	TCB[index].buffer[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long int) stack_top);
	TCB[index].buffer[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int)start_thunk); 
	TCB[index].buffer[0].__jmpbuf[JB_R12] = (unsigned long int)start_routine;
	TCB[index].buffer[0].__jmpbuf[JB_R13] = (unsigned long int) arg; 

		

	// add to schedule
	schedule[schedule_last] = index;
	schedule_last++;	
	thread_count++;	
	return 0;	
}

void pthread_exit(void *value_ptr){	

	TCB[current_thread].thread_id = 0;
	TCB[current_thread].thread_status = 3;
	free(TCB[current_thread].stack_pointer);
	thread_count--;
	update();
	scheduler();

	__builtin_unreachable();
}




