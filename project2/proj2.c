#include <stdio.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

sem_t* mutex;
FILE* fPtr;

void init();
void createSemaphores();
void destroySemaphores();
void skier();
void brick();
void skiBus();
int randomNum(int max);
void printBuffer(FILE* fPtr, const char* format, ...);

//typedef shared data structure, to make my life easier
typedef struct sData {
	int skiers;
	int stopAmt;
	int busCapacity;
	int maxWaitTime;
	int maxTravelTime;
	int line;
	int queue[10];	//array for tracking queues at each stop
	int sharedId;	//shared id, so every skier has a unique one
	int busAboard;	//tracker for amount of skiers aboard the bus
	sem_t stops[10];
	sem_t bus[10];	//bus semaphore, opening stops for skiers
	sem_t allAboard;//all aboard semaphore, signalling the bus to leave
	sem_t printing;	//printing semaphore to make sure prints happen one after the other
	sem_t finalStop;//finalStop semaphore, to let skiers off the bus
	sem_t departing;//departing semaphore, so that skiers exit one by one
} data;

data* shared;
int main(int argc, char **argv){

	//get main pid for SIGKILLs
	pid_t mainPid = getpid();

	shared = mmap(NULL, sizeof(data), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	//the only allowed argcount is 6, since there aren't any default values set for each of the data vars
	if (argc != 6){
		fprintf(stderr, "ERROR: wrong amount of arguments passed!\n");
		exit(1);
	}
	//
	//RESOLVING INPUT
	//
	for (int i = 1; i < argc; i++){
		switch(i){
			case 1:
				shared->skiers = atoi(argv[i]);
				if (shared->skiers >= 20000 || shared->skiers < 1){
					fprintf(stderr, "ERROR: Invalid amount of skiers, valid amount is 1 - 19999\n");
					exit(1);
				}
				break;
			case 2:
				shared->stopAmt = atoi(argv[i]);
				if (shared->stopAmt > 10 || shared->stopAmt < 1){
					fprintf(stderr, "ERROR: Invalid amount of stops, valid amount is 1 - 10\n");
					exit(1);
				}
				break;
			case 3:
				shared->busCapacity = atoi(argv[i]);
				if (shared->busCapacity < 10 || shared->busCapacity > 100){
					fprintf(stderr, "ERROR: Invalid bus capacity, valid bus capacity is 10 - 100\n");
					exit(1);
				}
				break;
			case 4:
				shared->maxWaitTime = atoi(argv[i]);
				if (shared->maxWaitTime > 10000 || shared->maxWaitTime < 0){
					fprintf(stderr, "ERROR: Invalid maximum waiting time, valid maximum waiting time is 0 - 10000\n");
					exit(1);
				}
				break;
			case 5:
				shared->maxTravelTime = atoi(argv[i]);
				if (shared->maxTravelTime > 1000 || shared->maxTravelTime < 0){
					fprintf(stderr, "ERROR: Invalid maximum travel time, valid maximum travel time is 0 - 1000\n");
					exit(1);
				}
				break;
			default:
				fprintf(stderr, "ERROR: Fatal exception, how did you achieve this?\n");
				break;
			}
	}

	//
	//END RESOLVING INPUT
	//

	//intiate all semaphores, shared data structure and file pointer

	init();

	setbuf(fPtr, NULL);
	
	//create bus process
	pid_t busProcess = fork();

	//if bus process is negative, error, brick and sigkill main with all child processes
	if (busProcess < 0){
		fprintf(stderr, "ERROR: Failed to fork bus!\n");
		brick();
		kill(-1*mainPid, SIGKILL);
		exit(1);
	}

	//if bus process is 0, start the bus
	if (busProcess == 0){

		//print message via printBuffer
		printBuffer(fPtr, "BUS: started\n");

		//while true cycle, making sure, BUS runs until all skiers have been delivered
		while(1){
			//cycle the bus through all the stops
			for (int i = 0; i < shared->stopAmt; i++){

				//usleep, simulating travel time
				usleep(randomNum(shared->maxTravelTime));
				skiBus(i);
			}

			//if the BUS has travelled through all the stops, it arrives at final
			printBuffer(fPtr, "BUS: arrived to final\n");

			//if there are people aboard the bus, open finalStop semaphore and wait for skier to disembark
			while (shared->busAboard != 0){
				sem_post(&shared->finalStop);
				sem_wait(&shared->departing);
			}

			//if there are no more skiers to be transported, break out of the while loop
			if (shared->skiers == 0){
				break;
			}

			//if there are more skiers to be transported, leave final and continue
			printBuffer(fPtr, "BUS: leaving final\n");
		}

		//print to satisfy the output format
		printBuffer(fPtr, "BUS: leaving final\n");

		//ending the bus
		printBuffer(fPtr, "BUS: finish\n");

		exit(0);

	}
	
	//establish skier amount for main, so it doesn't change (value in shared will)
	int skierAmt = shared->skiers;

	//generate all skier child processes
	for (int i = 0; i < skierAmt; i++){

		pid_t id = fork();

		//if id is negative, error,brick and SIGKILL everything, LET NONE SURVIVE!
		if (id < 0){
			fprintf(stderr, "ERROR: Failed to fork a skier process!\n");
			brick();
			kill(-1*mainPid, SIGKILL);
			exit(1);
		}

		//if ID ok, start skier process
		if (id == 0){

			//assign skierID, determined by the shared ID
			int skierId = shared->sharedId;

			//increment sharedId, to make sure each skier gets a unique one
			shared->sharedId++;

			//print start message
			printBuffer(fPtr, "L %i: started\n", skierId);
	
			//usleep, simulating breakfast time :)
			usleep(randomNum(shared->maxWaitTime));

			//start skier function
			skier(skierId, randomNum(shared->stopAmt));

			//once the function is exited, wait for finalStop semaphore to open
			sem_wait(&shared->finalStop);

			//decrement busAboard and skiers, since skiers have gone to ski
			shared->busAboard--;
			shared->skiers--;

			//print going to ski message
			printBuffer(fPtr, "L %i: going to ski\n", skierId);

			//open departing semaphore to make sure, while in bus continues
			sem_post(&shared->departing);

			//end skier process
			exit(0);
		}
	}

	//wait for all processes to end
	while(wait(NULL) > 0);

	//then brick
	brick();

	//end main process
	return 0;
}

//init function
void init(){

	//intiate queue with zeroes
	for (int i = 0; i < 10; i++){
		shared->queue[i] = 0;
	}

	//set shareId to 1, busAboard to 0 and line to 1
	shared->sharedId = 1;
	shared->busAboard = 0;
	shared->line = 1;

	//create all semaphores
	createSemaphores();

	//attempt fopen of proj2.out in write mode
	fPtr = fopen("proj2.out", "w");

	//if file opening failed, brick and end main
	if (fPtr == NULL){
		fprintf(stderr, "ERROR: Failed to open output file!\n");
		brick();
		exit(1);
	}
}

//create semaphores function
void createSemaphores(){

	//initiate all semaphores in arrays up to the amount of stops
	for (int i = 0; i < shared->stopAmt; i++){
		sem_init(&shared->stops[i], 1, 1);
		sem_init(&shared->bus[i], 1, 0);
	}

	//initiate all regular semaphores
	sem_init(&shared->allAboard, 1, 0);
	sem_init(&shared->printing, 1, 1);
	sem_init(&shared->finalStop, 1, 0);
	sem_init(&shared->departing, 1, 0);
}

//skier function, id is skierId and stop is the stop the skier is heading to
void skier (int id, int stop){

	//wait for desired stop to open (closed when bus present), skiers come one by one
	sem_wait(&shared->stops[stop]);

	//once open, add 1 to stops queue
	shared->queue[stop]++;

	//print arrived message
	printBuffer(fPtr, "L %i: arrived to %i\n", id, stop + 1);

	//open stop semaphore again, so another skier may arrive
	sem_post(&shared->stops[stop]);

	//while true cycle, to make sure deadlocks do not occur
	while (1){

		//wait for the stops bus semaphore to open (the bus has arrived and has capacity for more skiers)
		sem_wait(&shared->bus[stop]);

		//if there is space on the bus, start boarding
		if (shared->busAboard < shared->busCapacity){

			//print boarding message
			printBuffer(fPtr, "L %i: boarding\n", id);

			//decrement stops queue, add to busAboard as a skier has just boarded the bus
			shared->queue[stop]--;
			shared->busAboard++;

			//if the queue is empty, or the bus has reached its capacity, signal allAboard, let the bus go
			if (shared->queue[stop] == 0 || shared->busAboard == shared->busCapacity){
				sem_post(&shared->allAboard);

			}
			break;
		} else {

			//if there was no space on the bus, signal allAboard, let the bus drive on
			sem_post(&shared->allAboard);
		}
	}

	return;
}

//brick function == teardown
void brick(){

	//destroy all array semaphores
	for (int i = 0; i < shared->stopAmt; i++){
		sem_destroy(&shared->stops[i]);
		sem_destroy(&shared->bus[i]);
	}

	//destroy all regular semaphores
	sem_destroy(&shared->allAboard);
	sem_destroy(&shared->printing);
	sem_destroy(&shared->finalStop);
	sem_destroy(&shared->departing);

	//unmap shared data
	munmap(shared, sizeof(data));

	//close file
	fclose(fPtr);
	return;
}

//skibus function
void skiBus(int stop){

	//once bus has arrived at a stop, close its semaphore, so no more skiers may arrive
	sem_wait(&shared->stops[stop]);

	//print arrival message
	printBuffer(fPtr, "BUS: arrived to %i\n", stop + 1);
	
	// declare limit and space variables
	int limit;
	int space;

	//calculate space on bus
	space = shared->busCapacity - shared->busAboard;

	//whichever (space or queue) is lower will be set as the limit
	if ( space > shared->queue[stop]){
		limit = shared->queue[stop];
	} else {
		limit = space;
	}

	//open semaphore as many times as the limit, effectively letting on only as many passengers as there are spots on the bus
	for (int i = 0; i < limit; i++){
		sem_post(&shared->bus[stop]);
	};

	//if the queue at the stop is empty, or the bus has reached its capacity
	if (shared->queue[stop] == 0 || shared->busAboard == shared->busCapacity){

		//print a leaving message
		printBuffer(fPtr, "BUS: leaving %i\n", stop + 1);

		//and open the stop to let more passengers arrive at it
		sem_post(&shared->stops[stop]);
		
		//return out of function
		return;
	}

	//if there are skiers in the queue and the bus has at least 1 spot available
	//wait for the allAboard signal
	sem_wait(&shared->allAboard);

	printBuffer(fPtr, "BUS: allAboard\n");

	//then print a leaving message
	printBuffer(fPtr, "BUS: leaving %i\n", stop + 1);

	//and open the stop
	sem_post(&shared->stops[stop]);

	//return from function
	return;
}

//random number generator, taking max as its argument to determine the maximum number it can generate
int randomNum(int max){

	//set random seed as sharedId (changing almost constantly during the need for randomNum)
	srand(shared->sharedId);

	//generate a random value take the modulo of max, effectively limiting the range of the random value
	int num = rand() % (max);

	//return generated number
	return num;
}

//buffer printing
void printBuffer(FILE* fPtr, const char * format, ...){

	//close printing semaphore, ensuring only one process prints at a time
	sem_wait(&shared->printing);
	va_list args;
	va_start(args, format);
	fprintf(fPtr, "%i: ", shared->line);
	vfprintf(fPtr, format, args);
	shared->line++;
	fflush(fPtr);
	va_end(args);
	//open printing semaphore after the print has finished
	sem_post(&shared->printing);
	return;
}
