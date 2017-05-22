#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "declarations.h"
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
int main(int argc, char* argv[]) 
{
	if(argc != 3) {
		printf("format: ./executable input_file num_workers\n");
		return 0;
	}
	char* input_file = argv[1];
	int num_procs = atoi(argv[2]);
		
	FILE *fp;    
	char *line = NULL;
  size_t len = 0;
  ssize_t read;

  fp = fopen(input_file, "r");
  if (fp == NULL){
		printf("failure in creation of file pointer\n");
		exit(1);
	}
      
	//gauge size of Account and Transfer arrays
	int accounts_size = 0;
	int transfers_size = 0;
  while ((read = getline(&line, &len, fp)) != -1) {
		line = strtok(line, "\n");
		char** tokens = str_split(line, ' ');
		char* first_token = *tokens;
		if(strcmp(first_token, "Transfer") == 0)
			++transfers_size;
		else
			++accounts_size;
	}
		
	//initiate mapping and shared state;
	State st = (State) mmap(NULL, sizeof(struct state), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	st->accounts = (Account) mmap(NULL, accounts_size *  sizeof(struct account), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	Transfer transfers = (Transfer) malloc(transfers_size * sizeof(struct transfer));
	
	//rewind file to start processing again
	rewind(fp);

	//populating accounts and transfers
	int accounts_index = 0;
	int transfers_index = 0;
	while ((read = getline(&line, &len, fp)) != -1) {
		line = strtok(line, "\n");
		char** tokens = str_split(line, ' ');
		char* first_token = *tokens;
		if(strcmp(first_token, "Transfer") == 0){
			Transfer t = &(transfers[transfers_index]);
			t->out = *(tokens + 1);
			t->in = *(tokens + 2);
			t->amount = atoi(*(tokens + 3));
			++transfers_index;			
		}
		else{
			(st->accounts)[accounts_index].name = *tokens;
			(st->accounts)[accounts_index].balance = atoi(*(tokens + 1));
			++accounts_index;
		}
	}

	//close file 
	fclose(fp);
	if (line)
  	free(line);

	//initializing accesses map
	st->accesses = (Account_Access) mmap(NULL, accounts_size *  sizeof(struct account_access), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	for(int i = 0; i < accounts_size; ++i){
		Account_Access aa = &((st->accesses)[i]);
		aa->name = (st->accounts)[i].name;
		aa->access = 0;
	}

	//initializing attributes
	int ret;
	pthread_mutexattr_t psharedm;
	pthread_condattr_t psharedc;

	ret = pthread_mutexattr_init(&psharedm);
	if (ret) {
		printf("Mutex attribute initialization failed:  %d", ret);
		exit(1);
	}

  ret = pthread_mutexattr_setpshared(&psharedm, PTHREAD_PROCESS_SHARED);
	if (ret) {
		printf("Mutex attribute initialization failed:  %d", ret);
		exit(1);
	}

	ret = pthread_condattr_init(&psharedc);
	if (ret) {
		printf("Cond var attr initialization failed:  %d", ret);
		exit(1);
	}

	ret = pthread_condattr_setpshared(&psharedc,PTHREAD_PROCESS_SHARED);
	if (ret) {
		printf("Cond var attr initialization failed:  %d", ret);
		exit(1);
	}

	//initialize mutex and cond variable: Just 1 mutex and 1 cond var shared by all
	ret = pthread_mutex_init(&st->lock, &psharedm);
  if (ret) {
		printf("Mutex initialization failed:  %d", ret);
		exit(1);
	}
              
	ret =  pthread_cond_init(&st->cond, &psharedc);
	if (ret) {
		printf("Condition variable initialization failed:  %d", ret);
		exit(1);
	}
		
	//spawning num_procs processes 
	pid_t pids[num_procs];

	// start children.
	for (int index = 0; index < num_procs; ++index) {
  	pids[index] = fork();
		if (pids[index] < 0) {
    	perror("fork");
    	abort();
  	} else if (pids[index] == 0) {
			//round-robin logic here	
			while(index < transfers_size){
				Transfer transfer = &(transfers[index]);
				char* out_account_name = transfer->out;
				char* in_account_name = transfer->in;
				int amount = transfer->amount;

				//reading accounts in parallel
				int out_index = find_account(st->accounts, accounts_size, out_account_name);
				int in_index = find_account(st->accounts, accounts_size, in_account_name);

				int a1, a2;
				
				///////////////////transfer start
				pthread_mutex_lock(&(st->lock));
				a1 = find_account_access(st->accesses, accounts_size, out_account_name);
				a2 = find_account_access(st->accesses, accounts_size, in_account_name);
				while(!((st->accesses)[a1].access == 0 && (st->accesses)[a2].access == 0))
					pthread_cond_wait(&st->cond, &st->lock); 
				(st->accesses)[a1].access = 1;
				(st->accesses)[a2].access = 1;	
				pthread_mutex_unlock(&(st->lock));
				//////////////////
					
				((st->accounts)[out_index]).balance = ((st->accounts)[out_index]).balance - amount;
				((st->accounts)[in_index]).balance = ((st->accounts)[in_index]).balance + amount;
				
				//////////////////////transfer end
				pthread_mutex_lock(&st->lock);
				a1 = find_account_access(st->accesses, accounts_size, out_account_name);
				a2 = find_account_access(st->accesses, accounts_size, in_account_name);
				(st->accesses)[a1].access = 0;
				(st->accesses)[a2].access = 0;	
				pthread_cond_broadcast(&st->cond);	
				pthread_mutex_unlock(&st->lock);
				/////////////////////

				index = index + num_procs;
			}		
		
			//unmap child; order of unmapping is important
			munmap(st->accounts, accounts_size * sizeof(struct account));
			munmap(st->accesses, accounts_size * sizeof(struct account_access));
			munmap(st, sizeof(struct state));

			exit(0);
		}
	}
		
	//wait for children to exit.
	for(int i=0; i<num_procs; i++){
   	wait(NULL);
  }
		
	//print accounts
	for(int i = 0; i < accounts_size; ++i){
		printf("%s %d\n", (st->accounts)[i].name, (st->accounts)[i].balance);
	}

	//unmap parent
	munmap(st->accounts, accounts_size * sizeof(struct account));
	munmap(st->accesses, accounts_size * sizeof(struct account_access));
	munmap(st, sizeof(struct state));
	
	return 0;
}
