#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>


#define MAX_THREAD_NUM 5000
#define EXPIRE 3

pthread_mutex_t insertMutex;
pthread_mutex_t mutex;
pthread_mutex_t activeDel_lock;


//pthread_cond_t mutex_cond = PTHREAD_COND_INITIALIZER;

int active = 0;
int activeDeletes = 0;
int cond_spin = 1;

typedef struct list{
char *item;
struct list *next;
}Node;

Node *begin = NULL;
Node *end = NULL;

int searchThreads = 0;
int insertThreads = 0;
int deleteThreads = 0;

bool addToList(char *item){
	bool success = false;
	
	Node *new = NULL;
	new = malloc(sizeof(Node));
	if(new == NULL){
		fprintf(stderr,"insertToList : Malloc failed");
		return false;
	}

	char * buf = malloc(strlen(item) + 1);
	if(buf == NULL){
		fprintf(stderr,"insertToList : Malloc failed");
		return false;
	}

	strcpy(buf,item);
	new->item = buf;
	new->next = NULL;

	if(begin == NULL || end == NULL){
		begin = new;
		end = new;
		return true;
	}

	end->next = new;
	end = end->next;
	return true;
}

void printList(){
	if(begin == NULL)
		return;

	Node *temp = begin;

	printf("==============\n");
	while(temp!=NULL){
		printf("%s" , temp->item);
		temp = temp->next;
	}
}

bool removeFromList(char *item){
	if(end == NULL){
		return false;
	}

	Node *temp = begin;
	Node *prev = NULL;

	while(temp != NULL){
		if(strcmp(temp->item,item)== 0){
			if(prev == NULL){ //first node
				begin = begin->next;
				free(temp->item);
				free(temp);
				return true;
				}
			
			if(temp->next == NULL){ //last node
				end = prev;
			}

			prev->next = temp->next;
			free(temp->item);
			free(temp);
			return true;
		}	
		prev = temp;	
		temp = temp->next;
	}
	return false;
}

bool searchList(char *item){
	Node * temp;
	temp = begin;
	
	if(temp == NULL){
		return false;
	}

	while(temp != NULL){
		if(strcmp(temp->item,item) == 0){
			return true;
		}
		temp = temp->next;
	}
	return false;
}

void printThreadInfo(char* operation, char* value, bool success, pthread_t tid){
	int len = strlen(value);
	/*if(value[len-2] == '\r')
		printf("CR found\n");*/
	value[len-1] = '\0'; //remove the endline char
	if( value[len-2] == '\r')	
		value[len-2] = '\0'; //remove the carriage return char

	if(success)
		printf("[%08x]    Success %s [ %s ] Retrievers : %i Adders : %i Removers : %i\n" ,tid, operation,value,searchThreads,insertThreads,deleteThreads);
	else	
		printf("[%08x]    Fail %s [ %s ] Retrievers : %i Adders : %i Removers : %i\n" , tid , operation,value,searchThreads,insertThreads,deleteThreads);

}


void *searcher(void *args){
	int k;
	pthread_mutex_lock(&mutex); //mutex for shared variable
	active = active + 1;
	searchThreads = searchThreads + 1;
	pthread_mutex_unlock(&mutex);
	
	char* temp = (char*)args;
	bool success = searchList(temp);
	printThreadInfo("Search" , temp , success,pthread_self());
	pthread_mutex_lock(&mutex);
	active = active - 1;
	searchThreads = searchThreads - 1;
	if(active == 0 && activeDeletes>0){
	/*	for(k=0;k<activeDeletes;k++)
			pthread_cond_signal(&mutex_cond);
		}*/
		pthread_kill(pthread_self(),SIGUSR1);
	}
	pthread_mutex_unlock(&mutex);
}

void* inserter(void *args){
pthread_mutex_lock(&insertMutex);
int k;
	pthread_mutex_lock(&mutex);
	active = active + 1;
	insertThreads = insertThreads + 1;
	pthread_mutex_unlock(&mutex);

	char* temp = (char*)args;
	bool success = addToList(temp);
	printThreadInfo("Add" , temp , success,pthread_self());
	pthread_mutex_lock(&mutex);	
	active = active - 1;
	insertThreads = insertThreads - 1;
	if(active == 0 && activeDeletes>0){
/*		for(k=0;k<activeDeletes;k++)
			pthread_cond_signal(&mutex_cond); 
*/
		pthread_kill(pthread_self(),SIGUSR1);
	}
	pthread_mutex_unlock(&mutex);
pthread_mutex_unlock(&insertMutex);
}

void * deleter(void *args){
pthread_mutex_lock(&mutex); 
	while(active != 0){
		pthread_mutex_lock(&activeDel_lock);
		activeDeletes = activeDeletes + 1;
		pthread_mutex_unlock(&activeDel_lock);
//		pthread_cond_wait(&mutex_cond,&mutex); //atomically(release lock,spin here on some lock
		pthread_mutex_unlock(&mutex);
		while(cond_spin);
		
		pthread_mutex_lock(&activeDel_lock);
		activeDeletes = activeDeletes-1;
		pthread_mutex_unlock(&activeDel_lock);

		pthread_mutex_lock(&mutex);
		}
	deleteThreads = deleteThreads + 1;
	char* temp = (char*)args;
	bool success = removeFromList(temp);
	printThreadInfo("Delete" , temp , success,pthread_self());
	deleteThreads = deleteThreads - 1;
pthread_mutex_unlock(&mutex);
}

void initialize(void) 
{
	pthread_mutex_init(&mutex, NULL);
	pthread_mutex_init(&insertMutex, NULL);
	pthread_mutex_init(&activeDel_lock, NULL);
}

void signal_handler() {
//      pthread_mutex_lock(&cond_mutex);
        cond_spin = 0;
        while(activeDeletes!=0);
        cond_spin=1;
//      pthread_mutex_unlock(&cond_mutex);

}


int main(int argc , char** argv){
	pthread_t th[MAX_THREAD_NUM];
	char* th_arg[MAX_THREAD_NUM];
	int counter=0;
	char c;
	int l;
	int j;
	int len;

        struct sigaction cond_action;

        sigemptyset(&cond_action.sa_mask);
        sigaddset(&cond_action.sa_mask,SIGUSR1);
        cond_action.sa_flags = SA_RESTART;
	cond_action.sa_handler = signal_handler;

        sigaction(SIGUSR1,&cond_action,NULL);


	setbuf(stdout,NULL);

	 char filename[] = "p2_input.txt";
	 FILE *file = fopen ( filename, "r" );
	   if ( file != NULL )
	   {
	      char line [ 128 ]; /* or other suitable maximum line size */
	      while ( fgets ( line, sizeof line, file ) != NULL ) /* read a line */
      		{
		char *token; 
		token = strtok(line, " ");
		c = token[0];
                token = strtok(NULL, " ");
			 switch(c){
				case 'A' :		
					th_arg[counter] = malloc(strlen(token) * sizeof(char) + 1);
					strcpy(th_arg[counter],token);
					pthread_create(&(th[counter]), NULL, inserter, (void*)(th_arg[counter]));			
					counter++;
				break;
				
				case 'D' :
					th_arg[counter] = malloc(strlen(token) * sizeof(char) + 1);
					strcpy(th_arg[counter],token);
					pthread_create(&(th[counter]), NULL, deleter, (void*)(th_arg[counter]));			
					counter++;
				break;
				
				case 'M' :		
				for ( j = 0 ; j<counter ; j++ ){
						if(th[j] != NULL){
						pthread_join(th[j], NULL);
						th[j] = NULL;
						free(th_arg[j]);
					}
					}
				counter=0;
				break;
				
				case 'R' : 
					th_arg[counter] = malloc(strlen(token) * sizeof(char) + 1);
					strcpy(th_arg[counter],token);
					pthread_create(&(th[counter]), NULL, searcher, (void*)(th_arg[counter]));			
					counter++;
				break;
				
				default  :
					printf("Input error : %c", c);
			} 
		}
		printList();
		fclose ( file );
	   }	
	   else
	   {
	      perror ( filename ); /* why didn't the file open? */
	   }
	   return 0;
}
