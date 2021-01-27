#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stddef.h>

#define SHMKEY 724521634            //custom key for shared memory
#define ACTIVEPROCESSLIMIT 18       //Limit for the number of active processes
#define PROBOFTERM 2                //Probability of a process terminating
#define SIZEOFINT sizeof(int)       //Size of integer
#define MESSAGESIZE 8               //Size of a message in message queue
#define PCBSIZE sizeof(pcb)         //Size of process control block

int shmid0;                 //variable to store id for shared memory segment 0
int shmid1;                 //variable to store id for shared memory segment 1
int shmid2;                 //variable to store id for shared memory segment 2
int *clockSim;              //shared memory pointer to array

typedef struct      //process control block
{
    int class;
    int totalCPUtime;
    unsigned int startTimeInSystemSec;
    unsigned int startTimeInSystemNano;
    int lastBurstTime;
    int localPID;
    int processPriority;
    int preemptPercentage;
    unsigned int blockStartTimeSec;
    unsigned int blockStartTimeNano;
    unsigned int currentWaitTimeSec;
    unsigned int currentWaitTimeNano;
} pcb;
pcb *arrayOfPCB;

int* clockSec;          //Pointers to clock simulator
int* clockNano;
unsigned int seed = 1;
struct mBuffer{         //message buffer for messaging queue
    long mType;
    int mQuantum;
    int mState;     // 0=terminated  1=all quantum used  2=blocked 3=preempted
} messageQueue;

void childCleanup()                     //function to detach child from shared memory
{
    shmdt(clockSim);                                                  //detach from shared memory
    shmdt(arrayOfPCB);                                               //detach from shared memory
}

int getRandomNumber(int min, int max)       //function to return a random number
{
    srand(time(0)+(seed++) * (unsigned int)getpid());
    int randomNumber = (min + (rand() % ((max - min) +1 )));
    return randomNumber;
}

void receiveMessage()               //function to receive message from parent
{
    msgrcv(shmid2, &messageQueue, MESSAGESIZE, getpid(), 0);
}

void sendMessage(int quantumUsed, int state)        //function to send message to parent
{
    messageQueue.mType = getppid();
    messageQueue.mQuantum = quantumUsed;
    messageQueue.mState = state;

//    printf("Child Send: Data sent: %d %d\n", messageQueue.mState, messageQueue.mQuantum);

    msgsnd(shmid2, &messageQueue, MESSAGESIZE, 0);
}

int getPartialQuantumUsage()        //generates partial quantum usage with random number
{
    return getRandomNumber(0,messageQueue.mQuantum);
}

int checkIfTerminate()          //will decide if process will be terminating based on probability of termination definition
{
    int randomNumber = getRandomNumber(1,100);
    if(randomNumber > 0 && randomNumber <= PROBOFTERM)
    {
        return 1;
    } else
    {
        return 0;
    }
}

int startProcess()          //main function to run each time process is "doing something"
{
    receiveMessage();

    int quantumUsed;
    int processState;

    if(messageQueue.mQuantum == 80)         //Real time class
    {
        if(checkIfTerminate() == 1)                         //if process is terminating
        {
            processState = 0;
            quantumUsed = getPartialQuantumUsage();
            sendMessage(quantumUsed,processState);
        } else {
            processState = 1;
            quantumUsed = messageQueue.mQuantum;
            sendMessage(quantumUsed,processState);
        }
    } else {                                //User Class
        int stateDeterminer = getRandomNumber(0,2);
        if(checkIfTerminate() == 1)                         //if process is terminating
        {
            processState = 0;
            quantumUsed = getPartialQuantumUsage();
            sendMessage(quantumUsed,processState);
        } else if (stateDeterminer == 0)        //if all quantum used
        {
            processState = 1;
            quantumUsed = messageQueue.mQuantum;
            sendMessage(quantumUsed,processState);
        } else if(stateDeterminer == 1)
        {                                        //if process is blocked
            processState = 2;
            quantumUsed = getPartialQuantumUsage();
            sendMessage(quantumUsed,processState);
        } else {                                //Preempt state
            processState = 3;
            quantumUsed = getRandomNumber(0,99);
            sendMessage(quantumUsed,processState);
        }
    }

    return processState;
}

void handle_sigterm()                    //signal handler for terminate signal
{
    childCleanup();                     //function call to clean up child shared memory
    exit(1);
}

void printClock()       //Debug clock simulator display
{
    printf("Child Clock: %u:%u\n",*clockSec,*clockNano);
}


int main(int argc, char *argv[])
{
    signal(SIGTERM, handle_sigterm);        //initialization of termination signal handler

    shmid0 = shmget(SHMKEY, 2*SIZEOFINT, 0600 | IPC_CREAT);     //creates shared memory id for the clock simulator
    if (shmid0 == -1)
    {
        perror("Shared memory0C:");                           //if shared memory does not exist print error message and exit
        return 1;
    }

    clockSim = (int *)shmat(shmid0, 0, 0);                  //attaches to the shared memory clock simulator


    clockSec = &clockSim[0];                                 //creates a pointer to the clock simulator to be used in the increment clock function
    clockNano = &clockSim[1];

    shmid1 = shmget(SHMKEY+1, ACTIVEPROCESSLIMIT * PCBSIZE, 0600 | IPC_CREAT);  //creates shared memory id for the process control block
    if (shmid1 == -1)
    {
        perror("Shared memory1C:");                           //if shared memory does not exist print error message and exit
        return 1;
    }
    arrayOfPCB = (pcb *)shmat(shmid1, 0, 0);

    shmid2 = msgget(SHMKEY+2, 0600 | IPC_CREAT);            //creates shared memory id for messaging queue
    if (shmid2 == -1)
    {
        perror("Shared memory2C:");                           //if shared memory does not exist print error message and exit
        return 1;
    }

    int continueProcess = 99;
    while(continueProcess != 0)
    {
        continueProcess = startProcess();
    }
    childCleanup();
    exit(0);
}