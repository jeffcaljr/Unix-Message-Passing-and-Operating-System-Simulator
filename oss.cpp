#include <iostream>
#include <fstream>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <fcntl.h>  

using namespace std;

void spawn();
void incrementClock();
//void quitHandler(int); //keeps parent from killing itself when killing children
void interruptHandler(int); //Signal handler for master to handle Ctrl+C interrupt
void timerSignalHandler(int); //Signal handler for master to handle time out
void releaseMemory();


typedef struct clock{
	int seconds;
	int nanoseconds;
} os_clock;


os_clock *oss_clock;
int fd;

/**********************************************/
/************MESSAGE PASSING INITIALIZATION ***/
const int MSGSZ = 10;

const long CRITSEC_MESSAGE_TYPE = 1001;
const long OSS_MESSAGE_TYPE = 1002;

//USED FOR ENFORCEMENT OF CRITICAL SECTIONS AMONG USER PROCESSES
typedef struct CRITSEC_MESSAGE{
	long mtype;
	char mtext[MSGSZ];
} CritSecMessage;

//USED FOR PASSING OF OSS CLOCK TIME BETWEEN OS SIMULATOR AND USER PROCESSES
typedef struct oss_message{
	long mtype;
	int seconds;
	int nanoseconds;
} OSSMessage;

int mkey = ftok(".", 0); //MESSAGE QUEUE KEY FOR OSS<->USER_PROCESS COMMUNICATION
int childmkey = ftok(".", 1); //MESSAGE QUEUE KEY FOR USER_PROCESS<->USER_PROCESS CRITICAL SECTION ENFORCEMENT

int msqid; //MESSAGE QUEUE ID FOR OSS<->USER_PROCESS COMMUNICATION
int childmsqid; //MESSAGE QUEUE ID FOR USER_PROCESS<->USER_PROCESS CRITICAL SECTION ENFORCEMENT

/************************************************/


int numProcessesRunning = 0;
int processesSpawned = 0;
int startTime; //Time that the program will start monitoring it's run time
int maxRunTime; //Used to hold max seconds master should run; passed between functions

int main(int argc, char **argv){

	signal(SIGUSR1, SIG_IGN); //OSS simulator will ignore SIGUSR1
	signal(SIGTERM, SIG_IGN); //OSS simulator will ignore SIGTERM
	signal(SIGINT, interruptHandler);
	signal(SIGUSR2, timerSignalHandler);

	ofstream file;


	int numOfSlaves = 5; //number of slaves user wants to spawn
	int maxDuration = 20; //Number of seconds before master terminates itself
	const char *filename = "test.out"; //Output file name slave processes will write to

	/*****************************************************************************************/
	/*****************************CONFIGURE GETOPTS ******************************************/
	extern char *optarg; //used for getopt
	int c; //used for getopt

	int x, z; //temporary variables for command line arguments

	while((c = getopt(argc, argv, "hs:l:i:t:")) != -1){
		switch(c){
			case 'h': //help option
				cout << "This program accepts the following command-line arguments:" << endl;
				cout << "\t-h: Get detailed information about command-line argument options." << endl;
				cout << "\t-s x: Specify maximum number of slave processes to spawn (default 5)." << endl;
				cout << "\t-l filename: Specify the output file for the log (default 'test.out')." << endl;
				cout << "\t-t z: Specify time (seconds) at which master will terminate itself (default 20)." << endl;

				exit(0);
			break;

			case 's': //# of slaves option
				x = atoi(optarg);
				if(x < 0){
					cerr << "Cannot spawn a negative number of slaves." << endl;
					exit(1);
				}
				else{
					numOfSlaves = x;
				}
			break;

			case 'l': //filename option
				filename = optarg;
			break;

			case 't': //time at which master will terminate
				z = atoi(optarg);

				if(z < 0){
					cerr << "Master cannot have a run duration of negative time." << endl;
					exit(1);
				}
				else{
					maxDuration = z;
				}
			break;

			default:
				cerr << "Default getopt statement" << endl; //FIXME: Use better message.
				exit(1);
			break;
		}

	}

	/*****************************************************************************************/

	maxRunTime = maxDuration;

	//INITIALIZE OSS_CLOCK IN MEMORY

	fd = shm_open("/ossclock", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd == -1){
		//handle error
	}

	if (ftruncate(fd, sizeof(os_clock)) == -1){
		//handle error
	}

	oss_clock = (os_clock *) mmap(NULL, sizeof(os_clock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (oss_clock == MAP_FAILED){
		//handle error
	}

	oss_clock->seconds = 0;
	oss_clock->nanoseconds = 0;

	//OPEN MESSAGE QUEUES
	
	//MESSAGE WILL BE PASSED AMONG USER PROCESSES TO ENFORCE CRITICAL SECTION
	CritSecMessage cs_msg;
	cs_msg.mtype = CRITSEC_MESSAGE_TYPE;

	//WILL ACT AS BUFFER TO HOLD MESSAGES SENT FROM USER PROCESSES TO OSS SIMULATOR
	OSSMessage oss_msg;
	//oss_msg.mtype = OSS_MESSAGE_TYPE;

	
	//OPEN MESSAGE QUEUE BETWEEN OSS AND CHILDREN
	if((msqid = msgget(mkey, IPC_CREAT|0666)) == -1){
		cerr << "parent: msgget: msgget failed" << endl;
		exit(1);
	}
	else{
		//cout << "parent: msgget: message queue opened" << endl;
	}

	//OPEN MESSAGE QUEUE FOR CHILD PROCESSES TO COMMUNICATE
	if((childmsqid = msgget(childmkey, IPC_CREAT|0666)) == -1){
		cerr << "parent: msgget: msgget failed" << endl;
		exit(1);
	}
	else{
		//cout << "parent: msgget: message communication queue opened" << endl;
	}

	//SEND MESSAGE TO USER PROCESSES TO INITIALIZE THE CRITICAL SECION MESSAGE QUEUE
	
	
	if(msgsnd(childmsqid, &cs_msg, MSGSZ, IPC_NOWAIT) == -1){
		cerr << "Error sending message: " << errno << endl;
	}
	else{
		//cout << "parent: sent message" << endl;
	}


	//OPEN OUTPUT FILE FOR WRITING
	file.open(filename, ofstream::out | ofstream::app);

	startTime = time(0);

	//spawn specified number of user processes
	 for(int i = 0; i < numOfSlaves; i++){
	 	spawn();
	 }


	int childPid;

	while(oss_clock->seconds < 2 && processesSpawned < 100 && (time(0) - startTime) < maxDuration){

		incrementClock();
		
		if((msgrcv(msqid, &oss_msg, sizeof(int) * 2, OSS_MESSAGE_TYPE, IPC_NOWAIT)) == -1){
			//no message recieved
			continue;
			
		}
		else{ //message recieved from user process
			childPid = wait(NULL);
			--numProcessesRunning;
			//cout << "A:" << numProcessesRunning << " processes running" << endl;

			if(childPid != -1){
				file << "Master: Child " << childPid  << " is terminating at my time "
		 		 << oss_clock->seconds << ":" << oss_clock->nanoseconds
		 		 << " because it reached " << oss_msg.seconds << ":" << oss_msg.nanoseconds 
		 		 << endl;
			}

		 	spawn();
		}
		
	}

	if(oss_clock->seconds >= 2){
		cout << "finished via oss clock" << endl;
	}
	else if(processesSpawned > 100){
		cout << "finished via 100 spawns" << endl;
	}
	else if(time(0) - startTime >= maxDuration){
		cout << "finished via timeout" << endl;
	}


	cout << "Releasing memory..." << endl;

	releaseMemory();

	return 0;
}


void spawn(){
	++processesSpawned; //FIXME: Should this be inside fork???
	++numProcessesRunning;
	//cout << "B:" << numProcessesRunning << " processes running" << endl;
	cout << processesSpawned << " processes spawned" << endl;
	if(fork() == 0){
	 	execl("./user", "user", (char *)NULL);
	 	exit(0);
	 }
}


void incrementClock(){
	int amount = 1000;
	//cout << "nanoseconds is " << oss_clock->nanoseconds << endl;
	int newNano = oss_clock->nanoseconds + amount;
	int secondsToAdd = newNano / 1000000000;
	oss_clock->seconds = oss_clock->seconds + secondsToAdd;
	oss_clock->nanoseconds = newNano % 1000000000;
	//cout << "Clock time: " << oss_clock->seconds << ": " << oss_clock->nanoseconds << endl;
}


void interruptHandler(int signal){
	releaseMemory();
	killpg(getpid(), SIGTERM);
	sleep(2);

	// while(numProcessesRunning > 0){
	// 	wait(NULL);
	// 	--numProcessesRunning;
	// 	cout << "C:" << numProcessesRunning << " processes running" << endl;
	// }
	cout << "Exiting master process" << endl;
	// exit(0);
	killpg(getpid(), SIGKILL);
}

void timerSignalHandler(int signal){

	if(time(0) - startTime >= maxRunTime){
		cout << "time: " << time(0) << endl;
		cout << "starttime: " << startTime << endl;
		cout << "runtime: " << maxRunTime << endl;
	 	cout << "Master: Time's up!\n";
	 	killpg(getpid(), SIGUSR1);
	 	sleep(2);

		// while(numProcessesRunning > 0){
		// 	wait(NULL);
		// 	--numProcessesRunning;
		// 	cout << "D:" << numProcessesRunning << " processes running" << endl;
		// }
		releaseMemory();
		cout << "Exiting master process" << endl;
	 	killpg(getpid(), SIGKILL);
		exit(0);
	 }
}

//Handles case where parent wants to kill all children with its pid as their group id
//but not the parent itself
// void quitHandler(int signal){
// 	if(signal == SIGUSR1 || signal == SIGTERM){
// 		//do Nothing
// 	}
// }

void releaseMemory(){
	//kill off any remaining user processes
	killpg(getpid(), SIGTERM);
	sleep(2);
	// while(numProcessesRunning > 0){
	// 	wait(NULL);
	// 	--numProcessesRunning;
	// 	cout << "E:" << numProcessesRunning << " processes running" << endl;
	// }
	//Delete message queues and oss clock allocation
	msgctl(msqid, IPC_RMID, NULL);
	msgctl(childmsqid, IPC_RMID, NULL);
	shm_unlink("/ossclock");
	killpg(getpid(), SIGKILL);
}