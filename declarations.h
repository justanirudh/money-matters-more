#include<pthread.h>

struct account{
  char* name;
  int balance;
};

typedef struct account* Account;

struct transfer {
	char* out;
	char* in;
	int amount;
};

typedef struct transfer* Transfer;

struct account_access{
	char* name;
	int access; //0 means available, 1 means not available
};

typedef struct account_access* Account_Access;

struct state {
	Account accounts; //shared 
	pthread_mutex_t lock; //shared
	pthread_cond_t cond; //shared
	Account_Access accesses; //shared
};

typedef struct state* State;

char** str_split(char*, const char);
int find_account(Account, int, char*);
int find_account_access(Account_Access, int, char*);
