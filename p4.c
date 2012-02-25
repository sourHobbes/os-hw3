#include<stdio.h>
#include<string.h>
#include<assert.h>
#include<stdbool.h>
#include<pthread.h>
#include "myatomic.h"
#define MAX_MOVIE_LEN 51
#define MAX_COUNTRY_LEN 51
#define MAX_LINE_LEN 100
#define MAX_NUM_COUNTRY 5
#define NUM_THREADS_PER_COUNTRY 5
#define MAX_BUFFER_SIZE 1024*1024*10 // 10 mb
#define HASH_TABLE_SIZE 257

#define print(string) write(1,string,strlen(string))

//#define DEBUG
//#define VERBOSE

pthread_mutex_t debugLock = PTHREAD_MUTEX_INITIALIZER;

struct block {
        int sIndex;
        int eIndex;
} offsets[MAX_NUM_COUNTRY][NUM_THREADS_PER_COUNTRY] ;


struct movieInfo {
        long int numVotes;
        int rating;
        char movie[MAX_MOVIE_LEN];
};

struct node {
	int year;
	char country[MAX_COUNTRY_LEN];
	struct movieInfo *m;
	struct node *next;
};

typedef struct node Node; 

Node *hashTable[HASH_TABLE_SIZE];
/*
struct hashNode {
	Node *begin;
	Node *end;
} hashTable[HASH_TABLE_SIZE];
*/

struct threadInfo {
	int countryIndex;
	int threadId;
};

typedef struct threadInfo ThreadInfo;

char countries[MAX_NUM_COUNTRY][MAX_COUNTRY_LEN];
char buffer[MAX_NUM_COUNTRY][MAX_BUFFER_SIZE];
int numCountries;
int hashLock=1;

/*
*local low level lock based on compare and swap to ensure atomicity 
*/

static int lll_lock(int* lll_val) {
        while(1) {
                while(*lll_val!=1) {
                }
                        if(compare_and_swap(lll_val,0,1))  {
                                return;
                        }//spin here
        }
}
/*
*unlocks the low level lock
*/
static int lll_unlock(int* lll_val) {
        *lll_val=1;
}

int getCountries(char *fileName) {
        FILE *fp;
        fp = fopen(fileName,"r");
        assert(fp);
        int i=0;
#ifdef DEBUG
	printf("Countries:\n=========================\n");
#endif
        while(i < MAX_NUM_COUNTRY && fscanf(fp,"%[^\n]\n",countries[i++])==1 ) {
#ifdef DEBUG
		printf("%s\n",countries[i-1]);
#endif
	}

        fclose(fp);
        return i-1;
}

int searchCountry(char *c) {
	int i=0;
	for(i=0; i < numCountries;i++) {
		if(strcmp(c,countries[i])==0) {
			return i;
		}

	}

	return -1;
}

int addToBuffer(char *line,char *country) {
	int index = searchCountry(country);
	if(index == -1) {
		printf("ERROR: Country : %s not found in the country list\n",country);
		return -1;
	}
	int len = strlen(line);
	line[ len ] = '\n' ;
	line[ len+1 ] = '\0';
	strcat(buffer[index],line);
	return index;

}

void fillBuffer(char *fileName) {
	char fName[50]={0};
	char *cName;
	char line[MAX_LINE_LEN];
	FILE *fp, *fp1;
        fp = fopen(fileName,"r");
        assert(fp);
        int i=0,len=0;
        while( fscanf(fp,"%[^\n]\n",fName )==1 ) {
		if(fName[strlen(fName)-1] == ' ')
			fName[strlen(fName)-1] = '\0';
		fp1 = fopen(fName,"r");
#ifdef DEBUG
		printf("=======================\nFile Name: %s\n",fName);
#endif
		while( fscanf(fp1,"%[^\n]\n",line) == 1 ) {
#ifdef DEBUG
			printf("%s\n",line);
#endif

			len = strlen(line);
			i=len-1;
			while(i >= 0 && line[i] != ':'  ) {
				i--;
			}
			cName = line+i+1;
#ifdef DEBUG
			printf("---Country : %s----\n", cName );
#endif

			addToBuffer(line,cName );
		}

		fclose(fp1);
	}

        fclose(fp);

}

int findOffsets(char *fileContent, int threadCount,struct block * fBlocks ) {
        int blockSize,i,start=0,end=0,size=0;
        char newLine;

	size = strlen(fileContent);
	
        int tc=threadCount;
        int remChars = size;
        for(i=0;i<threadCount && end+1 < size;i++) {
                blockSize = remChars/tc;
                if( (start+blockSize-1 ) <= size ) {
                        end=end+blockSize;
                }
                do {
                        newLine = fileContent[end++];
                } while(newLine!='\n' && newLine!='\0');

/*
		if(newLine == '\n')
			printf("Newline found\n");
		else if(newLine == '\0')
			printf("Null found\n");
		else
			printf("Should not be printed\n");

		printf("i:%d End : %d size: %d\n",i,end , size);
*/		end--;
		(fBlocks+i)->sIndex = start;
		(fBlocks+i)->eIndex = end;
		remChars=size-end;

		tc--;
		start=end+1;
        }

        return size;
}
int hashFunc(int year,char *country) {
	
	int hashVal=0,i=0;
	for(i=0; i<strlen(country); i++) {
		hashVal += country[i] * (i+1) ;
	}

	hashVal+=year;
	hashVal = hashVal%HASH_TABLE_SIZE;
	return hashVal;	
}
/*
bool removeLowerRatingFromList(int index , char *country,long int numVotes , int rating , int year ) {
        if(hashTable[index].end == NULL){
                return false;
        }

        Node *temp = hashTable[index].begin;
        Node *prev = NULL;

        while(temp != NULL){
                if(strcmp(temp->country,country) == 0 && temp->year == year &&  \
		    ( temp->rating < rating || \
		      (temp->rating == rating && temp->numVotes < numVotes))  ) {
                        if(prev == NULL){
				
                                hashTable[index].begin = hashTable[index].begin->next;
				if (hashTable[index].begin == NULL)
					hashTable[index].end = NULL;
                                free(temp);
//                                return true;
				continue;
                                }

                        if(temp->next == NULL){
                                hashTable[index].end = prev;
                        }

                        prev->next = temp->next;
                        free(temp);
                        return true;
                }
                prev = temp;
                temp = temp->next;
        }
        return false;
}
*/


//bool insert(int index,char *movie,char *country,long int numVotes,int rating,int year) {
bool insert(char *movie,char *country,char *numVotes,char *rating,char *year) {
        bool success = false;

	bool newFlag = false;
	bool updateFlag = false;

	

	long int nVotes = atol(numVotes);
	int r = atoi(rating);
	int yr = atoi(year);
	int index;
	index = hashFunc(yr,country);

	struct movieInfo *m1 = NULL ,  *oldVal=NULL;
	m1 = malloc(sizeof(struct movieInfo));
	if(m1 == NULL) {
		fprintf(stderr,"insert : malloc failed");
		return false;
	}

	// m1 is used to update the entry with higher rated movie
        strcpy(m1->movie , movie);
        m1->numVotes = nVotes;
        m1->rating = r ;


	// new is used in case of new element in the hash index or same rated entry added in the collision linked list	
	Node *new = NULL;
	new = malloc(sizeof(Node));
	if(new == NULL) {
		fprintf(stderr,"insert: Malloc failed");
		return false;
	}

	strcpy(new->country , country);
	new->m = m1;
        new->year = yr ;
	new->next = NULL;

	Node *temp = NULL;

	while( success == false) {

	if ( hashTable[index] == NULL  ) {
//compareandswapptr(&hashTable[index].begin,NULL,new) .if fails go up and try again.move the memory allocation outside

//		hashTable[index].begin = new;
//		hashTable[index].end = new;
		if( compare_and_swap(&hashTable[index],new,NULL) == NULL) {
			success = true;
			newFlag = true;
			break;
		} else {
#ifdef VERBOSE
			print("CAS failed: While adding new node\n");			
#endif		
			continue;	
		}

	} else {

		temp = hashTable[index];
		oldVal = temp->m;  // checked later when adding multiple entries with same rating and during updating 
		if ( compare_and_swap(&(temp),hashTable[index] , hashTable[index]) != hashTable[index] ) {
			continue;
		}
		while(temp!=NULL) {

			if( strcmp(temp->country,country) == 0 && temp->year == yr ) {
				if ( (oldVal->rating < r ) ||   (oldVal->rating == r && oldVal->numVotes < nVotes ) )  {

#ifdef DEBUG
					printf("Replacing with new movie : %s\n" ,movie );
#endif
					// Update the existing entry with the high ranking entry


					if(compare_and_swap(&(temp->m),m1,oldVal) == oldVal ) {
						updateFlag = true;			
						success = true;	
					} else {
#ifdef VERBOSE
	                        		print("CAS failed: While updating node\n");
#endif						
						break;

					}

					// Remove existing entries with old values which are lower
					Node *t = temp->next ;
					Node *prev ;
					Node *toRemove;

					if(t!=NULL) { 
						if( compare_and_swap(&(hashTable[index]->m),m1,m1) != m1 ) {
							updateFlag=false;
							break; // some other thread has updated the value .. let that thread remove the old entries

						}
						lll_lock(&hashLock);
						Node *t1 = temp->next; // setting again since some other threads might have already removed lower rating movies
						prev=temp;
			
						while( t!= NULL && t1 == t ) { // if some other thread have removed and inserted some other new node then t1 == t condition will fail

							if ( strcmp(t->country,country)==0 && t->year == yr ) {
								toRemove = t;
							
#ifdef DEBUG
								printf("Removing old movie : %s\n" ,toRemove->m->movie );
#endif
								prev->next = t->next;	
								t=t->next;
								free(toRemove);
								
							} else {
								prev = t;
								t = t->next;
							}
						}

						lll_unlock(&hashLock);
					}

				} else if ( oldVal->rating == r && oldVal->numVotes == nVotes  ) {
#ifdef DEBUG
					printf("Adding at the end : %s %d %ld\n",movie,r,nVotes);
#endif
					lll_lock(&hashLock);
					if( compare_and_swap(&(hashTable[index]->m),oldVal,oldVal) != oldVal ) {
						lll_unlock(&hashLock);
						success = true;
                                                break; // some other thread has updated the value ..

                                        }


					Node *p = hashTable[index];
					while(p->next != NULL)
						p=p->next;

					p->next = new ;
					newFlag=true; // new node is used . if this flag is false then new can be freed.
					lll_unlock(&hashLock);
					success = true;
					break;
					// New node is inserted . Job is done so return
				} else {
					success = true; // new node rating is lower than existing one
				} 


			}

				if(success==true)
					break;

				if( compare_and_swap(&(hashTable[index]->m),oldVal,oldVal) != oldVal ) { // if value is updated then start again
					break; // break out of inner loop and success value will still be false so it will continue the process again
				}
				temp = temp->next;
		}
		
/*old
		removeLowerRatingFromList(index,country,new->numVotes,new->rating,new->year);

		if ( hashTable[index].begin == NULL || hashTable[index].end == NULL ) {
			hashTable[index].begin = new;
			hashTable[index].end = new;
		} else {

			hashTable[index].end->next = new;
			hashTable[index].end = hashTable[index].end->next ;
		}

*/
	}
	

	}

/*
	if(updateFlag == false)
		free(m1);
	if(newFlag == false)
		free(new); */ 
        return true;
}

void printFunc() {
	int i=0;
	Node *p;
	for(i=0;i< HASH_TABLE_SIZE; i++ ) {
		p=hashTable[i];
		while(p != NULL) {
			printf("%d => %s:%s:%ld:%d:%d\n",i,p->m->movie,p->country,p->m->numVotes,p->m->rating,p->year);
			p=p->next;
		}
	}

}



void *threadFunc( void *info ) {
	ThreadInfo *tInfo = (ThreadInfo *)info;


#ifdef DEBUG
	printf("Country:%s ThreadNum:%d\n",countries[tInfo->countryIndex],tInfo->threadId);
#endif
	int index = tInfo->threadId;
	int cIndex = tInfo->countryIndex;
	char movie[MAX_MOVIE_LEN];
	char country[MAX_COUNTRY_LEN];
	char votes[10];
	char rating[5];
	char year[5];
	
	int start = offsets[cIndex][index].sIndex;
	int end = offsets[cIndex][index].eIndex;

	if(start == 0 && end == 0)
		return;
	int i=0;
	do {

//		pthread_mutex_lock(&debugLock);
//		pthread_mutex_unlock(&debugLock);
		//Fetch Movie Name
		while(buffer[cIndex][start] != ':' ) {
			movie[i] = buffer[cIndex][start];
			i++;
			start++;
		}
		movie[i] = '\0';
		i=0;
		start++;

		//Fetch number of votes
                while(buffer[cIndex][start] != ':' ) {
                        votes[i] = buffer[cIndex][start];
                        i++;
                        start++;
                }
                votes[i] = '\0';
                i=0;
		start++;

		//Fetch rating
                while(buffer[cIndex][start] != ':' ) {
                        rating[i] = buffer[cIndex][start];
                        i++;
                        start++;
                }
                rating[i] = '\0';
                i=0;
                start++;


		//Fetch year
                while(buffer[cIndex][start] != ':' ) {
                        year[i] = buffer[cIndex][start];
                        i++;
                        start++;
                }
                year[i] = '\0';
                i=0;
                start++;

		//Fetch country
                while(buffer[cIndex][start] != '\n' && start <= end ) {
                        country[i] = buffer[cIndex][start];
                        i++;
                        start++;
                }
                country[i] = '\0';
                i=0;
		start++;
	
#ifdef DEBUG
		printf("=================\n");
		printf("Movie:%s\nVotes:%s\nRating:%s\nYear:%s\nCountry:%s\n",movie,votes,rating,year,country);
#endif			

		insert(movie,country,votes,rating,year);

	} while( start<end ); 	

}

int main() {


	numCountries = getCountries("countries.txt");
	fillBuffer("p4-in.txt");

	int i=0,j=0;
#ifdef DEBUG
	printf("==========Buffer Contents===========\n");
	for(i=0; i<numCountries; i++) {
		printf("%s",buffer[i]);
	}
#endif


	
	for(i=0;i<numCountries;i++ )	 {
		findOffsets(buffer[i],NUM_THREADS_PER_COUNTRY,offsets[i]);
	}

#ifdef DEBUG
	printf("======== Offsets =========\n");
	for(i=0;i< numCountries;i++) {
		printf("==== Country : %s ====\n" , countries[i]);
		for(j=0;j<NUM_THREADS_PER_COUNTRY ; j++ ) {
			printf("Thread: #%d start: %d end: %d\n",j,offsets[i][j].sIndex , offsets[i][j].eIndex);
		}
	}
#endif
/*
	insert(0,"movie","country","90000","7","2009");
	insert(0,"movie","country","90000","8","2009");
	insert(0,"movie1","country1","90000","7","2009");

//	insert(0,"movie2","country2",90000,7,2009);
//	insert(1,"movie2","country2",90000,7,2009);

	print();


	printf("hash of india ,1965 : %d\n",hashFunc(1965,"india"));
	printf("hash of USA ,1965 : %d\n",hashFunc(1965,"USA"));
	printf("hash of china ,1965 : %d\n",hashFunc(1965,"china"));
	printf("hash of japan ,1965 : %d\n",hashFunc(1965,"japan"));
	printf("hash of napaj ,1965 : %d\n",hashFunc(1965,"napaj"));*/


	ThreadInfo tInfo[MAX_NUM_COUNTRY][NUM_THREADS_PER_COUNTRY];
	pthread_t tid[MAX_NUM_COUNTRY][NUM_THREADS_PER_COUNTRY];

	for(i=0;i<numCountries;i++) {
		for(j=0;j<NUM_THREADS_PER_COUNTRY;j++) {
			tInfo[i][j].countryIndex = i ;
			tInfo[i][j].threadId = j;
			pthread_create(&tid[i][j],NULL,threadFunc,(void*)&tInfo[i][j] );
		}

	}


        for(i=0;i<numCountries;i++) {
                for(j=0;j<NUM_THREADS_PER_COUNTRY;j++) {
			pthread_join(tid[i][j],NULL);
		}
	}



/*	printf("----------");
	for(i=382;i<=563;i++) 
		printf("%c",buffer[0][i]);


//	findOffsets(buffer[2],NUM_THREADS_PER_COUNTRY,offsets[i]);
	ThreadInfo tInfo;
	tInfo.countryIndex = 0;
	tInfo.threadId = 4;
	threadFunc((void *)&tInfo);

//	printf("%s\n",buffer[0]+382);
*/
	printFunc();
	
}
