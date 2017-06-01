#include <iostream>
#include <ctime>
#include <cstdlib>
#include <signal.h>
#include <fcntl.h>  
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <sys/mman.h>



using namespace std;

typedef struct clock{
	int seconds;
	int nanoseconds;
} os_clock;




int fd;
os_clock *oss_clock;

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

/***************************************/
int getTime();
int sendMessageToOSS(int, OSSMessage *);
int forwardMessage(int, CritSecMessage *);
void terminateSigHandler(int); //handles signal from parent to terminate on Ctrl+C
void timeoutSigHandler(int); //handles signal from parent to terminate on timeout
/***********Define Functions ***********/


int main(int argc, char **argv){

	signal(SIGTERM, terminateSigHandler);
	signal(SIGUSR1, timeoutSigHandler);

	/** Open shared memory and read system clock **/


	fd = shm_open("/ossclock", O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
	if (fd == -1){
		//handle error
		cerr << "Err 1" << endl;
	}


	if (ftruncate(fd, sizeof(os_clock)) == -1){
		//handle error
		//cerr << "Err 2" << endl;
		//FIXME: ERROR GENERATED HERE
	}

	oss_clock = (os_clock *) mmap(NULL, sizeof(os_clock), PROT_READ, MAP_SHARED, fd, 0);
	if (oss_clock == MAP_FAILED){
		//handle error
		cerr << "Err 3" << endl;
	}

	//OPEN MESSAGE QUEUES
	
	//MESSAGE WILL BE PASSED AMONG USER PROCESSES TO ENFORCE CRITICAL SECTION
	CritSecMessage cs_msg;
	cs_msg.mtype = CRITSEC_MESSAGE_TYPE;

	//WILL ACT AS BUFFER TO HOLD MESSAGES SENT FROM USER PROCESSES TO OSS SIMULATOR
	OSSMessage oss_msg;
	oss_msg.mtype = OSS_MESSAGE_TYPE;

	
	//OPEN MESSAGE QUEUE BETWEEN OSS AND CHILDREN
	if((msqid = msgget(mkey, IPC_CREAT|0666)) == -1){
		cerr << "child: msgget: msgget failed" << endl;
		exit(1);
	}
	

	//OPEN MESSAGE QUEUE FOR CHILD PROCESSES TO COMMUNICATE
	if((childmsqid = msgget(childmkey, IPC_CREAT|0666)) == -1){
		cerr << "child: msgget: msgget failed" << endl;
		exit(1);
	}

	int startTime = oss_clock->nanoseconds;


	srand(getTime());
	int runTime = (rand() % 100000) + 1; //OSS Clock duration that this process should run for

	while(msgrcv(childmsqid, &cs_msg, MSGSZ, CRITSEC_MESSAGE_TYPE, 0) != -1){
		
		//IN CRITICAL SECTION
				
		//If duration is up, message parent and exit
		if (getTime() - startTime >= runTime){
			cout << getpid() << "start time: " << startTime << " :run time: " << runTime << " : clock time: " << getTime() << endl;

			oss_msg.seconds = oss_clock->seconds;
			oss_msg.nanoseconds = oss_clock->nanoseconds;


			if(sendMessageToOSS(msqid, &oss_msg) == -1){
				cerr << getpid() << ": Error sending message to parent: " << endl;
				forwardMessage(childmsqid, &cs_msg);
				exit(1);
			}
			else{
				//Sent message to parent
				forwardMessage(childmsqid, &cs_msg);
				exit(0);
			}
		}
		else{ //Otherwise, forward crit section message to another process
			forwardMessage(childmsqid, &cs_msg);
		}
		
	}

	return 0;
}


int sendMessageToOSS(int msqid, OSSMessage *msg){
	if((msgsnd(msqid, msg, sizeof(int) * 2, 0)) == -1){
		return -1;
	}
	else{
		return 0;
	}
}

int forwardMessage(int msqid, CritSecMessage *msg){
	if(msgsnd(msqid, msg, MSGSZ, IPC_NOWAIT) == -1){
		//cerr << "Error sending message to com key: " << endl;
		return -1;
	}
	else{
		//cout << getpid() << ": sent message" << endl;
		return 0;
	}
	//exit(0);
}

int getTime(){
	int clockTime = (oss_clock->seconds * 1000000000) + oss_clock->nanoseconds;
	//cout << "Clock time: " << clockTime << endl;
	return clockTime; 
}


void terminateSigHandler(int signal){
	cout << getpid() << ": recieved interrupt signal" << endl;
	if(signal == SIGTERM){
		cerr << "Process " << getpid() << " exiting due to interrupt signal.\n";
		exit(1);
	}
}

void timeoutSigHandler(int signal){
	if(signal == SIGUSR1){
		cerr << "Process " << getpid() << " exiting due to timeout.\n";
		exit(0);
	}
}