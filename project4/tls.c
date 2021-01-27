#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/mman.h>
#include <string.h>

// *** TLS Globals ***
#define HASH_SIZE 128
#define DEFAULT -1


typedef struct thread_local_storage
{
	pthread_t tid;
	unsigned int size; /* size in bytes */
	unsigned int page_num; /* number of pages */
	struct page **pages; /* array of pointers to pages */
}TLS;

struct page
{
	unsigned long int address; /* start address of page */
	int ref_count; /* counter for shared pages */
};

struct hash_element
{
	pthread_t tid;
	TLS *tls;
	struct hash_element *next;
};

int tls_initialized = 0;
int page_size;

struct hash_element* hash_table[HASH_SIZE];
// *** *** *** *** *** *** *** *** *** *** *** *** ***

// *** *** HELPER FUNCTIONS  *** *** *** *** *** *** 

void tls_protect(struct page *p){
	if (mprotect((void *) p->address, page_size, 0)){
		fprintf(stderr, "tls_protect: could not protect page\n");
		exit(1);
	}
}

void tls_unprotect(struct page *p){
	if (mprotect((void *) p->address, page_size, PROT_READ | PROT_WRITE)){
		fprintf(stderr, "tls_unprotect: could not unprotect page\n");
		exit(1);
	}
}

void tls_handle_page_fault(int sig, siginfo_t *si, void *context){

	unsigned long int p_fault;

	p_fault = ((unsigned long int) si->si_addr) & ~(page_size - 1);

	// check where it occurred
	int i;
	int pidx;
	//int index = 0;
	int flag = 0;
	for(i = 0; i < HASH_SIZE; i++){
		for(pidx = 0; pidx < hash_table[i]->tls->page_num; pidx++){
			if(hash_table[i]->tls->pages[pidx]->address == p_fault){
				//index = i;
				flag = 1;
				pthread_exit(NULL);	
			}
		}
	}

	if(flag == 0){
		signal(SIGSEGV, SIG_DFL);
		signal(SIGBUS, SIG_DFL);
		raise(sig);

	}
	
}

void tls_init(){
	struct sigaction sigact;

	/* get the size of a page */
	page_size = getpagesize();
	
	/* install the signal handler for page faults (SIGSEGV, SIGBUS) */
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_SIGINFO; /* use extended signal handling */
	sigact.sa_sigaction = tls_handle_page_fault;

	sigaction(SIGBUS, &sigact, NULL);
	sigaction(SIGSEGV, &sigact, NULL);

	// initialize hash_table
	int i;
	for(i = 0; i < HASH_SIZE; i++){
		hash_table[i] = (struct hash_element*) calloc(1,sizeof(struct hash_element));
		hash_table[i]->tid = DEFAULT;
	}	

	tls_initialized = 1;	
}
// *** *** *** *** *** *** *** *** *** *** *** *** *** *** *** ***

// ****** TLS FUNCTIONS ********
int tls_create(unsigned int size){
	//lock();
	// error handling
	if ( !tls_initialized){
		tls_init();
	}
	if(size <= 0){
		//unlock();
		return -1;
	}
	// make sure doesn't already have TLS
	int i;
	pthread_t this_thread = pthread_self();
	for(i = 0; i < HASH_SIZE; i++){
		if(this_thread == hash_table[i]->tid){
			//unlock();
			return -1;
		}	
	}
	
	// allocate TLS using calloc
	TLS *new_entry =  (TLS*) calloc(1,sizeof(TLS));

	// initialize TLS
	new_entry->tid = pthread_self();
	new_entry->size = size;
	new_entry->page_num = (new_entry->size / page_size) + ((new_entry->size % page_size) != 0);
	
	// allocate TLS->pages, array of pointers using calloc
	struct page **page_array = (struct page**) calloc(new_entry->page_num,sizeof(struct page)); 
	new_entry->pages = page_array;

	// paginate all pages for this TLS	
	for(i = 0; i < new_entry->page_num; i++){
		struct page *p = (struct page*) calloc(1,sizeof(struct page));
		p->address = (unsigned long int) mmap(0, page_size, 0, MAP_ANON | MAP_PRIVATE, 0, 0);
		p->ref_count = 1;
		new_entry->pages[i] = p;		
	}
	// update mapping structures
	int index = 0;
	for(i = 0; i < HASH_SIZE; i++){
		if(hash_table[i]->tid == DEFAULT){
			index = i;
			break;
		}		
	}
	hash_table[index]->tid = this_thread;
	hash_table[index]->tls = new_entry;
	//unlock();
	return 0;
}

int tls_destroy(){
	if ( !tls_initialized){
		return -1;
	}
	// error handling
	int i;
	int index = 0;
	int flag = 0;
	pthread_t this_thread = pthread_self();
	for(i = 0; i < HASH_SIZE; i++){
		if(hash_table[i]->tid == this_thread){
			index = i;
			flag = 1;
			break;
		}
	}
	if(flag == 0){
		return -1;
	}	
	
	// clean up all pages 
	for(i = 0; i < hash_table[index]->tls->page_num; i++){
		if(hash_table[index]->tls->pages[i]->ref_count == 1){
			//free(hash_table[index]->tls->pages[i]->address);
			free(hash_table[index]->tls->pages[i]);
		} else if(hash_table[index]->tls->pages[i]->ref_count > 1){
			hash_table[index]->tls->pages[i]->ref_count--;
		}
	}	
	// clean up TLS
	free(hash_table[index]->tls);

	// remove mapping from global data structure
	hash_table[index]->tid = DEFAULT;
	
	return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer){
	if( !tls_initialized){
		return -1;
	}
	// error handling
	int i;
	int index = 0;
	int flag = 0;
	pthread_t this_thread = pthread_self();
	for(i = 0; i < HASH_SIZE; i++){
		if(hash_table[i]->tid == this_thread){
			index = i;
			flag = 1;
			break;
		}
	}
	if(flag == 0){
		return -1;
	
	}
	if(offset + length > hash_table[index]->tls->size){
		return -1;
	}
	// unprotect all pages belonging to thread's TLS
	for(i = 0; i < hash_table[index]->tls->page_num;i++){
		tls_unprotect(hash_table[index]->tls->pages[i]);
	}

	// perform read operation
	// test
	int cnt;
	int idx;
	char* src;
	for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) {
 		struct page *p;
 		unsigned int pn, poff;

 		pn = idx / page_size;
		poff = idx % page_size;

 		p = hash_table[index]->tls->pages[pn];
 		src = ((char *) p->address) + poff;

 		buffer[cnt] = *src;
 	}			
	
	// Reprotect all pages belonging to thread's TLS
	for(i = 0; i < hash_table[index]->tls->page_num;i++){
		tls_protect(hash_table[index]->tls->pages[i]);
	}
	return 0;
}

int tls_write(unsigned int offset, unsigned int length, char *buffer){
	if( !tls_initialized){
		return -1;
	}
	// error handling
	int i;
	int index = 0;
	int flag = 0;
	pthread_t this_thread = pthread_self();
	for(i = 0; i < HASH_SIZE; i++){
		if(hash_table[i]->tid == this_thread){
			index = i;
			flag = 1;
			break;
		}
	}
	if(flag == 0){
		return -1;
	}
	if(offset + length > hash_table[index]->tls->size){
		return -1;
	}
	// unprotect all pages belonging to thread's TLS
	for(i = 0; i < hash_table[index]->tls->page_num;i++){
		tls_unprotect(hash_table[index]->tls->pages[i]);
	}


	// perform write operation
	// test
	int cnt;
	int idx;
	char* dst;
	for (cnt = 0, idx = offset; idx < (offset + length); ++cnt, ++idx) {
 		struct page *p, *copy;
 		unsigned int pn, poff;
 		pn = idx / page_size;
 		poff = idx % page_size;
 		p = hash_table[index]->tls->pages[pn];
 		if (p->ref_count > 1) {
 			copy = (struct page *) calloc(1, sizeof(struct page));
			copy->address = (unsigned long int) mmap(0, page_size, PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
			copy->ref_count = 1;
			
		
			/* copy memory */
			memcpy( (void*) copy->address, (void*) p->address, page_size);			

			hash_table[index]->tls->pages[pn] = copy;
	
			/* update original page */
			p->ref_count--;
			tls_protect(p);
			p = copy;							
 		}
 		dst = ((char *) p->address) + poff;
 		*dst = buffer[cnt];
 	}			
	
	// reprotect all pages belonging to thread's TLS
	for(i = 0; i < hash_table[index]->tls->page_num;i++){
		tls_protect(hash_table[index]->tls->pages[i]);
	}

	return 0;
}

int tls_clone(pthread_t tid){
	if( !tls_initialized){
		return -1;
	}
	// error handling
	int i;
	//int this_index = 0;
	int target_index = 0;
	int this_flag = 0;
	int target_flag = 0;
	// check this thread
	pthread_t this_thread = pthread_self();
	for(i = 0; i < HASH_SIZE; i++){
		if(hash_table[i]->tid == this_thread && this_flag == 0){
			//this_index = i;
			this_flag = 1;
		}
		
		if(hash_table[i]->tid == tid && target_flag == 0){
			target_index = i;	
			target_flag = 1;
		}

		if(this_flag == 1 && target_flag == 1){
			break;
		}
	}
	if((this_flag == 1 && target_flag == 0) || (this_flag == 0 && target_flag == 0)){
		return -1;
	}

	// allocate TLS for this thread

	TLS *new_entry =  (TLS*) calloc(1,sizeof(TLS));

	// initialize TLS
	new_entry->tid = pthread_self();
	new_entry->size = hash_table[target_index]->tls->size;
	new_entry->page_num = hash_table[target_index]->tls->page_num;
	
	// allocate TLS->pages, array of pointers using calloc
	struct page **page_array = (struct page**) calloc(new_entry->page_num,sizeof(struct page)); 
	new_entry->pages = page_array;

	// paginate all pages for this TLS	
	for(i = 0; i < new_entry->page_num; i++){
		struct page *p = (struct page*) calloc(1,sizeof(struct page));
		p->address = hash_table[target_index]->tls->pages[i]->address;
		p->ref_count = hash_table[target_index]->tls->pages[i]->ref_count + 1;
		hash_table[target_index]->tls->pages[i]->ref_count = p->ref_count;
		new_entry->pages[i] = p;		
	}

	// update mapping structures
	int index = 0;
	for(i = 0; i < HASH_SIZE; i++){
		if(hash_table[i]->tid == DEFAULT){
			index = i;
			break;
		}		
	}
	hash_table[index]->tid = this_thread;
	hash_table[index]->tls = new_entry;
	// test			
	return 0;
}

// *****************************


