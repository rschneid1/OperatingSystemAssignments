#include <pthread.h>
#include <unistd.h> // ualarm
#include <stdlib.h> // malloc
#include <stdio.h> // printf
#include <signal.h>
#include <time.h>
#include <setjmp.h>
#include <sys/types.h>
#include <string.h>
#include <semaphore.h>
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
#define DEFAULT 999999

// * * * Globals * * * 
struct TCB_entry {
	pthread_t thread_id;
	int thread_status;
	int target_thread;
	void *stack_pointer;
	void *pthread_exit_return;
	//unsigned long int* stack_exit;
	jmp_buf buffer;
};

// create TCB Structure
struct TCB_entry TCB[MAX_THREAD_COUNT];
int schedule[MAX_THREAD_COUNT];
int blocked[MAX_THREAD_COUNT];

int thread_count = 0;
int current_thread = 0;
int thread_index = 0;
int schedule_last = 0;
int init_flag = 0;

timer_t timerid;

struct sigaction new_action;
sigset_t sigmask;

// lock and unlock variables
int lock_active = 0;

// * * * * * * * * * * 


pthread_t pthread_self(){
	return (pthread_t) current_thread;
}

/* Thread Status
* 0 : thread is ready
* 1 : thread is running
* 2 : thread is paused
* 3 : thread ran until completion
* 4 : thread is blocked
* 999999 : default value 
*/


//***  semaphores ****
struct sem_struct{
	unsigned int current_value;
	int init_flag;
	int q[MAX_THREAD_COUNT];
	int sem_index;

};
struct sem_struct SEM[MAX_THREAD_COUNT];



// places current thread to back of TCB , and shifts rest of entries to the left
void lock(){
	sigemptyset(&sigmask);
	sigaddset(&sigmask,SIGALRM);
	if(sigprocmask(SIG_BLOCK,&sigmask,NULL) == -1){
		printf("Could not lock ... \n");
		exit(0);
	}
	lock_active = 1;
}

void unlock(){
	if(lock_active == 1){
		// turn off lock first just incase preempted
		lock_active = 0;
		if(sigprocmask(SIG_UNBLOCK,&sigmask,&sigmask) == -1){
			printf("Could not unlock ... \n");
			exit(0);
		}
	}
}



// cycles schedule by one
void cycle(){
	int i;
	int temp = schedule[0];
	for(i = 0; i < schedule_last - 1; i++){
		schedule[i] = schedule[i + 1];
	}
	schedule[schedule_last - 1] = temp;
}

// deletes entry in schedule
void shift(int thread){
	int index;
	int i;
	int flag = 0;
	// find index of thread in schedule
	for(i = 0; i < schedule_last;i++){
		if(schedule[i] == thread){
			index = i;
			flag = 1;
			break;
		}
	}

	// remove thread and shift
	if(flag == 1){
		for(i = index; i < schedule_last;i++){
			schedule[i] = schedule[i + 1];
		}
		schedule_last--;
	}
}

// find threads that target this thread
void unblock(int index){
	int i;
	for(i = 0; i < MAX_THREAD_COUNT;i++){
		if(TCB[i].target_thread == index){
			TCB[i].thread_status = 2;
		}
	}	
}

// if a process is closed, remove from schedule
void update(){
	int i;
	for(i = 0; i < MAX_THREAD_COUNT;i++){
		if(TCB[i].thread_status == 3){
			unblock(i);
			shift(i);
		}		
	}	
}


void scheduler(){
	// round robin schedule
	if (setjmp(TCB[current_thread].buffer) == 0){
		lock();
		// make sure schedule not accidentally firing after exit
		if(TCB[current_thread].thread_status != DEFAULT && TCB[current_thread].thread_status != 4 && TCB[current_thread].thread_status != 3){
			TCB[current_thread].thread_status = 2; 
		}
		cycle();
		update();
		while(TCB[schedule[0]].thread_status == 4){
			cycle();
		}
		current_thread = schedule[0];
		TCB[current_thread].thread_status = 1;
		unlock();
		longjmp(TCB[current_thread].buffer, 1);	
	}
}



// ***** Semaphore Functions *****
int sem_init(sem_t *sem, int pshared, unsigned value){
	lock();
	// init an unnamed semaphore referred to by sem
	// pshared == 0
	// semaphore pointed to by sem is shared between threads of the process

	// find open space for semaphore
	int index;
	int i;
	for(i = 0;i < MAX_THREAD_COUNT; i++){
		if(SEM[i].init_flag == 0){
			index = i;
			break;
		}	

	}

	// init vars in sem structure
	SEM[index].current_value = value;
	SEM[index].init_flag = 1;
	SEM[index].sem_index = 0;
	for(i = 0; i < MAX_THREAD_COUNT; i++){
		SEM[index].q[i] = DEFAULT;
	}
	
	sem->__align = (unsigned int) index;

	unlock();
	return 0;
}

int sem_wait(sem_t *sem){
	lock();
	int index;
	index  = (int) sem->__align;
			
	if (SEM[index].current_value == 0){
		// block
		TCB[current_thread].thread_status = 4;
		SEM[index].q[SEM[index].sem_index] = current_thread;
		SEM[index].sem_index++;
		unlock();
		while(TCB[current_thread].thread_status == 4){
		}
		lock();
	
	}

	SEM[index].current_value--;
	unlock();
	return 0;
}

int sem_post(sem_t *sem){
	lock();
	int index;
	index = (int) sem->__align;

	// increment
	SEM[index].current_value++;
		
	if(SEM[index].q[0] != DEFAULT){	
		// unblock the first thread in the queue
		TCB[SEM[index].q[0]].thread_status = 2;
	}
	// shift queue
	int i;
	for(i = 0; i < SEM[index].sem_index; i++){
		SEM[index].q[i] = SEM[index].q[i+1];
	}
	
	SEM[index].sem_index--;
	if(SEM[index].sem_index < 0){
		SEM[index].sem_index = 0;
	}
	unlock();
	return 0;
}

int sem_destroy(sem_t *sem){
	lock();
	int index;
	index = (int) sem->__align;

	// overwrite its variables
	SEM[index].current_value = 0;
	SEM[index].init_flag = 0;
	int i;
	for(i = 0; i < MAX_THREAD_COUNT; i++){
		SEM[index].q[i] = DEFAULT;
	}
	SEM[index].sem_index = 0;

	unlock();
	return 0;
}


//********************


int system_init(){
	// init TCB
	int i;
	/* init default TCB values
	* 999999
	*/
	for(i = 0;i < MAX_THREAD_COUNT;i++){
		TCB[i].thread_status = DEFAULT;
		TCB[i].thread_id = DEFAULT;
		TCB[i].target_thread = DEFAULT;
		schedule[i] = DEFAULT;
	}
	
	// assign TCB for main
	TCB[0].thread_id = 999;
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
	// add to schedule
	schedule[schedule_last] = 0;
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
	lock();
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
		if (TCB[i].thread_status == DEFAULT){
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
	*stack_top = (unsigned long int) pthread_exit_wrapper;
	
	setjmp(TCB[index].buffer);	
	TCB[index].buffer[0].__jmpbuf[JB_RSP] = ptr_mangle((unsigned long int) stack_top);
	TCB[index].buffer[0].__jmpbuf[JB_PC] = ptr_mangle((unsigned long int)start_thunk); 
	TCB[index].buffer[0].__jmpbuf[JB_R12] = (unsigned long int)start_routine;
	TCB[index].buffer[0].__jmpbuf[JB_R13] = (unsigned long int) arg; 

		

	// add to schedule
	schedule[schedule_last] = index;
	schedule_last++;	
	thread_count++;
	unlock();	
	return 0;	
}

int pthread_join(pthread_t thread, void **value_ptr){
	// use while loop in condition
	lock();
	// find TCB index
	int i;
	int index;
	for(i = 0; i < MAX_THREAD_COUNT; i++){
		if (TCB[i].thread_id == thread){
			index = i;
			break;
		}
	}

	// if thread has already terminated and collected, return
	if(TCB[index].thread_status == DEFAULT || TCB[index].thread_id == DEFAULT){
		//printf("Thread already terminated ... \n");
		unlock();
		return 0;
	}

	// waits for thread to terminate	
	TCB[current_thread].thread_status = 4;		
	TCB[current_thread].target_thread = index;
	unlock();
	while(TCB[current_thread].thread_status == 4){
	}
	lock();
	
	// reset calling thread
	TCB[current_thread].target_thread = DEFAULT;

	// collect and free
	TCB[index].thread_id = DEFAULT;
	TCB[index].thread_status = DEFAULT;
	TCB[index].target_thread = DEFAULT;
	free(TCB[index].stack_pointer);	

	if (value_ptr == NULL){
		TCB[index].pthread_exit_return = NULL;
		unlock();
		return 0;
	} else if (value_ptr != NULL){
		*value_ptr = TCB[index].pthread_exit_return;
		TCB[index].pthread_exit_return = NULL;
		unlock();
		return 0;
	}
	return 0;
}



void pthread_exit(void *value_ptr){	
	lock();
	// set thread to ran until completion
	TCB[current_thread].thread_status = 3;
	TCB[current_thread].pthread_exit_return = value_ptr;
	thread_count--;
	unlock();
	scheduler();

	__builtin_unreachable();
}




