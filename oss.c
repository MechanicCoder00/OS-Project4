#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <stddef.h>
#include <math.h>
#define SHMKEY 724521634            //custom key for shared memory
#define ACTIVEPROCESSLIMIT 18           //Limit for the number of active processes
#define PROBOFUSERPROCESS 90            //Probability of a generated process being a user process
#define QUANTUM 80                      //base quantum amount
#define TOTALPROCESSESLIMIT 100         //Limit for the total number of processes
#define SIZEOFINT sizeof(int)           //Size of integer
#define MESSAGESIZE 8                   //Size of a message in message queue
#define STARVATIONPREVENTIONLIMITSEC 15 //Seconds for a process to wait until getting moved to a higher priority queue
#define PCBSIZE sizeof(pcb)             //Size of process control block
#define MAXTIMEBETWEENNEWPROCSSECS 1    //Maximum seconds between new processes
#define MAXTIMEBETWEENNEWPROCSNS 1000   //Maximum nanoseconds between new processes

/*
 * Project  : Operating systems assignment 4
 * Author   : Scott Tabaka
 * Date Due : 3/30/2020
 * Course   : CMPSCI 4760
 * Purpose  : This program simulates an operating system process scheduling.
 */


int shmid0;                 //variable to store id for shared memory segment 0
int shmid1;                 //variable to store id for shared memory segment 1
int shmid2;                 //variable to store id for shared memory segment 2
int *clockSim;              //shared memory pointer to array
int activeChildren = 0;     //variable to keep track of active children
int totalProcessesLaunched = 0;
FILE *output;              //variable for storing auxiliary file output used for creating input files
char fileName[8] = "logfile";      //base name of the output file
char fileExt[5] = ".log";           //file extension for log file
char outputFileName[20];
int logFileCount = 0;
int logFileLineCount = 0;
char stringOutput[120];
char programName[20];

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
    unsigned int currentWaitStartTimeSec;
    unsigned int currentWaitStartTimeNano;
} pcb;
pcb *arrayOfPCB;

//for priority queue
int priorityQueue[4][ACTIVEPROCESSLIMIT+1];
int queueRear[4] = {-1,-1,-1,-1};

//for blocked queue
int blockQueue[ACTIVEPROCESSLIMIT+1][3];
int blockQueueRear = {-1};

int preemptedProcess = 0;
int* clockSec;              //Pointers to clock simulator
int* clockNano;
unsigned int seed = 1;      //helper number to create random numbers, increases each time it is used

struct mBuffer{         //message buffer for messaging queue
    long mType;
    int mQuantum;
    int mState;     // 0=terminated  1=all quantum used  2=blocked 3=preempted
} messageQueue;

int processControlTable[ACTIVEPROCESSLIMIT] = {0 }; //process control table
unsigned int nextProcessLaunchTimeSec;
unsigned int nextProcessLaunchTimeNSec;

//variables for program stats
int processesTerminatedSoFar=0;
unsigned int totalWaitTimeSec=0;
unsigned int totalWaitTimeNS=0;
double averageWaitTimeSec;
unsigned int totalCPUTime=0;
double averageCPUTime;
unsigned int totalCPUIdleTimeSec=0;
unsigned int totalCPUIdleTimeNS=0;
double finalCPUIdleTimeSec;
int timesProcessesBlocked=0;
unsigned int totalBlockWaitTimeSec=0;
unsigned int totalBlockWaitTimeNS=0;
double averageBlockWaitTimeSec;


void makeNewOutputFile()    //if log file is greater than 10000 lines make a new log file
{
    if(logFileCount > 0)
    {
        fclose(output);
    }

    strcpy(outputFileName,fileName);
    char str[5];
    snprintf(str, sizeof(str), "%d", logFileCount);
    strcat(outputFileName,str);
    strcat(outputFileName,fileExt);
    logFileCount++;
    logFileLineCount = 0;

    output = fopen(outputFileName, "w");
}

void checkOutputFileLineCount()     //function to check if log file line count is greater than 10000 lines
{
    if(logFileLineCount > 10000)
    {
        makeNewOutputFile();
    }
}

void printOutputToFile(char *string)    //function to print string to log file
{
    checkOutputFileLineCount();

    fprintf(output, "%s\n", string);

    logFileLineCount++;
}

void schedProcOutput(int pid, int priority, int class)
{
    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Dispatching");
    if(class == 0)
    {
        strcat(stringOutput, " REAL-TIME");
    } else
    {
        strcat(stringOutput, " USER");
    }
    strcat(stringOutput, " process with PID");
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " from queue");
    snprintf(str, sizeof(str), " %d", priority);
    strcat(stringOutput, str);
    strcat(stringOutput, " at time");
    snprintf(str, sizeof(str), " %d:%d", *clockSec, *clockNano);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void genProcOutput(int pid, int priority, int class)
{
    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Generating");

    if(class == 0)
    {
        strcat(stringOutput, " REAL-TIME");
    } else
    {
        strcat(stringOutput, " USER");
    }

    strcat(stringOutput, " process with PID");
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " and putting it in queue");
    snprintf(str, sizeof(str), " %d", priority);
    strcat(stringOutput, str);
    strcat(stringOutput, " at time");
    snprintf(str, sizeof(str), " %d:%d", *clockSec, *clockNano);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void receiveOutput(int pid, int time, int state)
{
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Receiving process with PID");
    char str[40];
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " ran for");
    snprintf(str, sizeof(str), " %d", time);
    strcat(stringOutput, str);
    strcat(stringOutput, " nanoseconds,");
    switch(state)
    {
        case 0:
            strcat(stringOutput, " process terminated");
            break;
        case 1:
            strcat(stringOutput, " process used all of quantum");
            break;
        case 2:
            strcat(stringOutput, " process blocked by I/O");
            break;
        case 3:
            strcat(stringOutput, " process pre-empted");
            break;
        default:
            break;
    }
    printOutputToFile(stringOutput);
}

void agingOutput(int pid, int priority1, int priority2)
{
    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Aging process by moving USER process with PID");
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " from queue");
    snprintf(str, sizeof(str), " %d", priority1);
    strcat(stringOutput, str);
    strcat(stringOutput, " into queue");
    snprintf(str, sizeof(str), " %d", priority2);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void resumeOutput(int pid)
{
    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Resuming preempted process with PID");
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " at time");
    snprintf(str, sizeof(str), " %d:%d", *clockSec, *clockNano);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void fromBlockOutput(int pid, int priority, int time)
{
    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Removing process with PID");
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " from blocked queue into queue");
    snprintf(str, sizeof(str), " %d", priority);
    strcat(stringOutput, str);
    strcat(stringOutput, " at time");
    snprintf(str, sizeof(str), " %d:%d", *clockSec, *clockNano);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);

    strcpy(stringOutput, programName);
    strcat(stringOutput, " Time taken to move process from block queue");
    snprintf(str, sizeof(str), " %d", time);
    strcat(stringOutput, str);
    strcat(stringOutput, " nanoseconds");
    printOutputToFile(stringOutput);
}

void toBlockOutput(int pid, unsigned int seconds, unsigned int nanoseconds)
{
    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Putting process with PID");
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " into blocked queue, process will wait until");
    snprintf(str, sizeof(str), " %d:%d", seconds, nanoseconds);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void enqueueOutput(int pid, int priority, int class)
{
    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Putting");

    if (class == 0) {
        strcat(stringOutput, " REAL-TIME");
    } else {
        strcat(stringOutput, " USER");
    }

    strcat(stringOutput, " process with PID");
    snprintf(str, sizeof(str), " %d", pid);
    strcat(stringOutput, str);
    strcat(stringOutput, " into queue");
    snprintf(str, sizeof(str), " %d", priority);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void dispatchTimeOutput(int time)
{
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Total time this dispatch was");
    char str[40];
    snprintf(str, sizeof(str), " %d", time);
    strcat(stringOutput, str);
    strcat(stringOutput, " nanoseconds");
    printOutputToFile(stringOutput);
}

void printClock()   //Debugging clock display
{
    printf("Parent Clock: %u:%u\n",*clockSec,*clockNano);
}

void printClockOutput()     //prints successful message with current program time to log file
{
    printOutputToFile("");

    char str[40];
    strcpy(stringOutput, programName);
    strcat(stringOutput, " Program completed successfully at time");
    snprintf(str, sizeof(str), " %d:%d", *clockSec, *clockNano);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void incrementClock(unsigned int secIncrement, unsigned int nanoIncrement)         //function to increment clock simulator
{
    *clockSec = (unsigned int)*clockSec + secIncrement;
    *clockNano = (unsigned int)*clockNano + nanoIncrement;                        //increments clock simulator.

    while((unsigned int)*clockNano >= 1000000000)                      //if nano seconds are equal or greater than 1 second
    {
        *clockSec = (unsigned int)*clockSec + 1;                        //increase seconds by 1
        *clockNano = (unsigned int)*clockNano - 1000000000;               //decrease nano seconds by 1 second
    }
}

void calculateCPUIdleTime() //calculates cpu idle time for program stats
{
    char str[40];
    double nanoSec = roundf((double)totalCPUIdleTimeNS/10000000)/100;
//    printf("Result:%f\n",nanoSec);
    finalCPUIdleTimeSec = (totalCPUIdleTimeSec) + nanoSec;

//    printf("Total CPU Idle Time: %.2fs\n",finalCPUIdleTimeSec);

    strcpy(stringOutput, "Total CPU Idle Time:");
    snprintf(str, sizeof(str), " %.2fs", finalCPUIdleTimeSec);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void addCPUIdleTime(unsigned int cpuIdleTimeSec, unsigned int cpuIdleTimeNS)    //adds cpu idle time to variable for program stats
{
    incrementClock(cpuIdleTimeSec,cpuIdleTimeNS);
    totalCPUIdleTimeSec = (unsigned int)totalCPUIdleTimeSec + (unsigned int)cpuIdleTimeSec;
    totalCPUIdleTimeNS = (unsigned int)totalCPUIdleTimeNS + (unsigned int)cpuIdleTimeNS;

    while((unsigned int)totalCPUIdleTimeNS >= 1000000000)                      //if nano seconds are equal or greater than 1 second
    {
        totalCPUIdleTimeSec = (unsigned int)totalCPUIdleTimeSec + 1;                        //increase seconds by 1
        totalCPUIdleTimeNS = (unsigned int)totalCPUIdleTimeNS - 1000000000;               //decrease nano seconds by 1 second
    }
}

void calculateAverageCPUTime()  //calculates average cpu time for program stats
{
    char str[40];
    averageCPUTime = (double)totalCPUTime/processesTerminatedSoFar;
//    printf("Total CPU Time: %.2d\n",totalCPUTime);
//    printf("Average CPU Time: %.2fns\n",averageCPUTime);

    strcpy(stringOutput, "Average CPU Time:");
    snprintf(str, sizeof(str), " %.2fns", averageCPUTime);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void addCPUTime(int cpuTime)    //adds cpu time to variable for program stats
{
    totalCPUTime = totalCPUTime + cpuTime;
}

void calculateAverageWaitTime() //calculates process average wait time for program stats
{
    char str[40];
    unsigned int procWaitTimeSec = totalWaitTimeSec;
    unsigned int procWaitTimeNS = totalWaitTimeNS;

    unsigned int divisor = processesTerminatedSoFar*10000000;

    double nanoSec = roundf(procWaitTimeNS/divisor)/100;
//    printf("Result:%f\n",nanoSec);
    averageWaitTimeSec = (procWaitTimeSec/processesTerminatedSoFar) + nanoSec;

//    printf("Average Wait Time: %.2fs\n",averageWaitTimeSec);

    strcpy(stringOutput, "Average Wait Time:");
    snprintf(str, sizeof(str), " %.2fs", averageWaitTimeSec);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void calculateWaitTime(unsigned int processStartSec, unsigned int processStartNS, int cpuTime)  //calculates total process wait time for program stats
{
    unsigned int totalTimeSec = (unsigned int)*clockSec-(unsigned int)processStartSec;
    unsigned int totalTimeNS;
    if(*clockNano > processStartNS)
    {
        totalTimeNS = (unsigned int)*clockNano-(unsigned int)processStartNS;
    } else {
        totalTimeNS = (unsigned int)processStartNS-(unsigned int)*clockNano;
    }

    unsigned int procWaitTimeSec = totalTimeSec;
    unsigned int procWaitTimeNS;
    if(totalTimeNS > cpuTime)
    {
        procWaitTimeNS = totalTimeNS - cpuTime;
    } else {
        procWaitTimeNS = cpuTime - totalTimeNS;
    }

    totalWaitTimeSec = totalWaitTimeSec + procWaitTimeSec;
    totalWaitTimeNS = totalWaitTimeNS + procWaitTimeNS;
}

void calculateBlockAverageWaitTime()    //calculates block average wait time for program stats
{
    char str[40];
    unsigned int procBlockWaitTimeSec = totalBlockWaitTimeSec;
    unsigned int procBlockWaitTimeNS = totalBlockWaitTimeNS;

    unsigned int divisor = timesProcessesBlocked*10000000;

    double nanoSec = roundf(procBlockWaitTimeNS/divisor)/100;
//    printf("Result:%f\n",nanoSec);
    averageBlockWaitTimeSec = (procBlockWaitTimeSec/processesTerminatedSoFar) + nanoSec;


//    printf("Average Block Wait Time: %.2fs\n",averageBlockWaitTimeSec);

    strcpy(stringOutput, "Average Block Wait Time:");
    snprintf(str, sizeof(str), " %.2fs", averageBlockWaitTimeSec);
    strcat(stringOutput, str);
    printOutputToFile(stringOutput);
}

void calculateBlockWaitTime(unsigned int processBlockStartSec, unsigned int processBlockStartNS)    //calculates block wait time for program stats
{
    unsigned int blockTimeSec = (unsigned int)*clockSec-(unsigned int)processBlockStartSec;
    unsigned int blockTimeNS;
    if(*clockNano > processBlockStartNS)
    {
        blockTimeNS = (unsigned int)*clockNano-(unsigned int)processBlockStartNS;
    } else {
        blockTimeNS = (unsigned int)processBlockStartNS-(unsigned int)*clockNano;
    }

    totalBlockWaitTimeSec = (unsigned int)totalBlockWaitTimeSec + blockTimeSec;
    totalBlockWaitTimeNS = (unsigned int)totalBlockWaitTimeNS + blockTimeNS;

    while((unsigned int)totalBlockWaitTimeNS >= 1000000000)                      //if nano seconds are equal or greater than 1 second
    {
        totalBlockWaitTimeSec = (unsigned int)totalBlockWaitTimeSec + 1;                        //increase seconds by 1
        totalBlockWaitTimeNS = (unsigned int)totalBlockWaitTimeNS - 1000000000;               //decrease nano seconds by 1 second
    }
}

void calculateStats(unsigned int processStartSec, unsigned int processStartNS, int cpuTime)     //adds to variables for program stats
{
    calculateWaitTime(processStartSec,processStartNS,cpuTime);
    addCPUTime(cpuTime);
}

void displayStats()     //Outputs program stats to the end of log output
{
    printOutputToFile("");
    printOutputToFile("");

    calculateAverageWaitTime();
    calculateAverageCPUTime();
    calculateCPUIdleTime();
    calculateBlockAverageWaitTime();
}

int findKey(int pid)        //function to find element number using pid
{
    int i;
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
        if(processControlTable[i] == pid)
        {
            return i;
        }
    }
    return -1;
}

void displayPCBdata()       //Debuging Process control block display
{
    int i;
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
//        if(arrayOfPCB[i].class == 0)
//        {
//            printf("Process:%d\n",i);
//            printf("Program PID:%d\n",processControlTable[i]);
//            printf("Class:%d\n",arrayOfPCB[i].class);
//            printf("\n");
//        }

        printf("Process:%d\n",i);
        printf("Program PID:%d\n",processControlTable[i]);
        printf("Class:%d\n",arrayOfPCB[i].class);
//        printf("Total CPU Time:%d\n",arrayOfPCB[i].totalCPUtime);
//        printf("Start Time: %d:%d\n",arrayOfPCB[i].startTimeInSystemSec,arrayOfPCB[i].startTimeInSystemNano);
//        printf("Last Burst Time:%d\n",arrayOfPCB[i].lastBurstTime);
//        printf("Local PID:%d\n",arrayOfPCB[i].localPID);
//        printf("Process Priority:%d\n",arrayOfPCB[i].processPriority);
//        printf("Block Start Time: %d:%d\n",arrayOfPCB[i].blockStartTimeSec,arrayOfPCB[i].blockStartTimeNano);

//        printf("Total Time on System: %d:%d\n",arrayOfPCB[i].startTimeInSystemSec,arrayOfPCB[i].startTimeInSystemNano);
//        calculateWaitTime(arrayOfPCB[i].startTimeInSystemSec,arrayOfPCB[i].startTimeInSystemNano,arrayOfPCB[i].totalCPUtime);
//        printf("Wait Time: %d:%d\n",arrayOfPCB[i].startTimeInSystemSec,arrayOfPCB[i].startTimeInSystemNano);
//        if(processesTerminatedSoFar > 0)
//        {
//            calculateAverageWaitTime();
//        }
        printf("Current Wait Start Time[%d]: %d:%d\n", i, arrayOfPCB[i].currentWaitStartTimeSec,arrayOfPCB[i].currentWaitStartTimeNano);

//        printf("\n");
    }
}

void displayPCT()       //Debuging Process control table display
{
    int i;
    printf("Process Control Table:\n");
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
        printf("[%d]:%d\n", i, processControlTable[i]);
    }
}

void displayBlockQueue()        //Debuging blocked queue display
{
    int i;
    printf("Block Queue: Rear: %d\n", blockQueueRear);
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
        printf("[%d] %d %d %d \n", i, blockQueue[i][0], blockQueue[i][1], blockQueue[i][2]);
    }
}

void displayQueue()         //Debuging priority queue display
{
    int i;
    for(i=0;i<4;i++)
    {
        printf("Priority Queue %d: ",i);
        int j;
        for(j=0;j<ACTIVEPROCESSLIMIT;j++)
        {
            if(priorityQueue[i][j] > 0)
            {
                printf("%d ",priorityQueue[i][j]);
            } else
            {
                printf("\n");
                break;
            }
        }
    }
    for(i=0;i<4;i++)
    {
        printf("QueueRear[%d]: %d\n", i, queueRear[i]);
    }
}

void displayAll()       //Debuging main display
{
//    displayPCT();
//    displayBlockQueue();
//    displayQueue();
//    displayPCBdata();
}

void enqueue(int priority, int pid)     //adds a process to the back of priority queue level
{
    if(priority > 1)
    {
        arrayOfPCB[findKey(pid)].currentWaitStartTimeSec = (unsigned int)*clockSec;
        arrayOfPCB[findKey(pid)].currentWaitStartTimeNano = (unsigned int)*clockNano;
    } else {
        arrayOfPCB[findKey(pid)].currentWaitStartTimeSec = 0;
        arrayOfPCB[findKey(pid)].currentWaitStartTimeNano = 0;
    }
    arrayOfPCB[findKey(pid)].processPriority = priority;
    int i;
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
        if(priorityQueue[priority][i] == 0)
        {
            priorityQueue[priority][i] = pid;
            queueRear[priority]++;
            break;
        }
    }
}

int dequeue(int priority)       //removes a process from the head of a priority queue
{
    int pid =  priorityQueue[priority][0];
    int i;
    for(i=0;i<(queueRear[priority]+1);i++)
    {
        priorityQueue[priority][i] = priorityQueue[priority][i+1];
        priorityQueue[priority][i+1] = 0;
    }
    queueRear[priority]--;

    arrayOfPCB[findKey(pid)].currentWaitStartTimeSec = 0;
    arrayOfPCB[findKey(pid)].currentWaitStartTimeNano = 0;

    return pid;
}

int removeElementFromMiddleOfQueue(int element, int priority)          //For removing any process from priority queue
{
    int pid =  priorityQueue[priority][element];
    int i;

    for(i=element;i<queueRear[priority]+1;i++)
    {
        priorityQueue[priority][i] = priorityQueue[priority][i+1];
        priorityQueue[priority][i+1] = 0;
    }
    queueRear[priority]--;

    arrayOfPCB[findKey(pid)].currentWaitStartTimeSec = 0;
    arrayOfPCB[findKey(pid)].currentWaitStartTimeNano = 0;

    return pid;
}

int calculateCurrentWaitTime(int pid)       //if pid is older than the AGEPREVENTIONLIMIT return 1
{
    unsigned int currentWaitTimeSec = 0;
    if(arrayOfPCB[findKey(pid)].currentWaitStartTimeSec != 0)
    {
        currentWaitTimeSec = (unsigned int)*clockSec-(unsigned int)arrayOfPCB[findKey(pid)].currentWaitStartTimeSec;
    }

    if(currentWaitTimeSec > STARVATIONPREVENTIONLIMITSEC)
    {
        return 1;
    }
    return 0;
}

int findPriorityQueueElement(int pid, int priority) //returns the element number the pid is found in
{
    int i;
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
        if(priorityQueue[priority][i] != 0)
        {
            if(priorityQueue[priority][i] == pid)
            {
                return i;
            }
        }
    }
    return -1;
}

int checkCurrentWaitTime()     //function to age processes by moving them to highest user priority queue
{
    int i;
    int priority;
    for(priority=2;priority<4;priority++)
    {
        if(queueRear[priority] >= 0)
        {
            for(i=0;i<(queueRear[priority]+1);i++)
            {
                int pid = priorityQueue[priority][i];
                int element = findPriorityQueueElement(pid,priority);
                int escalate = calculateCurrentWaitTime(pid);
                if(escalate == 1)
                {
                    removeElementFromMiddleOfQueue(element,priority);
                    agingOutput(pid, priority, 1);
                    enqueue(1,pid);                                 //raise user process to highest user priority
                    return 1;
                }
            }
        }
    }
    return 0;
}

int findNextInQueue()       //function to find next available process in priority queue
{
    int i;
    for(i=0;i<4;i++)
    {
        if(queueRear[i] >= 0)
        {
            return i;
        }
    }
    return -1;
}

int getRandomNumber(int min, int max)       //function to return a random number
{
    srand(time(0)+seed++);
    int randomNumber = (min + (rand() % ((max - min) +1 )));
    return randomNumber;
}

void enqueueBlock(int pid, unsigned int blockSec, unsigned int blockNS)     //Adds process to blocked queue
{
    int i;
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
        int tempPid;
        unsigned int tempSec;
        unsigned int tempNS;

        if(blockQueue[i][0] == 0)
        {
            blockQueue[i][0] = pid;
            blockQueue[i][1] = blockSec;
            blockQueue[i][2] = blockNS;
            break;
        } else if(blockSec < blockQueue[i][1] || (blockSec == blockQueue[i][1] && blockNS < blockQueue[i][2]))
        {
            tempPid = blockQueue[i][0];
            tempSec = blockQueue[i][1];
            tempNS = blockQueue[i][2];

            blockQueue[i][0] = pid;
            blockQueue[i][1] = blockSec;
            blockQueue[i][2] = blockNS;

            pid = tempPid;
            blockSec = tempSec;
            blockNS = tempNS;
        }
    }
    blockQueueRear++;
}

int dequeueBlock()          //Removes process from blocked queue
{
    int pid = blockQueue[0][0];

    int i;
    for(i=0;i<blockQueueRear+1;i++)
    {
        blockQueue[i][0] = blockQueue[i+1][0];
        blockQueue[i][1] = blockQueue[i+1][1];
        blockQueue[i][2] = blockQueue[i+1][2];

        blockQueue[i+1][0] = 0;
        blockQueue[i+1][1] = 0;
        blockQueue[i+1][2] = 0;
    }
    blockQueueRear--;

    unsigned int blockStartTimeSec = arrayOfPCB[findKey(pid)].blockStartTimeSec;
    unsigned int blockStartTimeNano = arrayOfPCB[findKey(pid)].blockStartTimeNano;

    if(blockStartTimeSec !=0 && blockStartTimeNano != 0)
    {
        calculateBlockWaitTime(blockStartTimeSec, blockStartTimeNano);
    }

    return pid;
}

int searchBlockQueueForReady()      //function determines if there is a process ready to come out of blocked queue
{
if(blockQueueRear >= 0)
{
    if(*clockSec > blockQueue[0][1] || (*clockSec == blockQueue[0][1] && *clockNano >= blockQueue[0][2]))
    {
        return 1;
    }
}
    return 0;
}

int setPriorityMultiplier(int priority)     //helps set process quantum from base value
{
    switch(priority)
    {
        case 0:
            return 1;
        case 1:
            return 2;
        case 2:
            return 4;
        case 3:
            return 8;
        default:
            return 99;
    }
}

//initializes process control block values
void initializePCB(int index, int class, int cpuTime, unsigned int startTimeSec, unsigned int startTimeNano, int lastBurst, int localPID, int priority)
{
    arrayOfPCB[index].class = class;
    arrayOfPCB[index].totalCPUtime += cpuTime;
    arrayOfPCB[index].startTimeInSystemSec += startTimeSec;
    arrayOfPCB[index].startTimeInSystemNano += startTimeNano;
    arrayOfPCB[index].lastBurstTime = lastBurst;
    arrayOfPCB[index].localPID = localPID;
    arrayOfPCB[index].processPriority = priority;
    arrayOfPCB[index].preemptPercentage = 0;
    arrayOfPCB[index].blockStartTimeSec = 0;
    arrayOfPCB[index].blockStartTimeNano = 0;
    arrayOfPCB[index].currentWaitStartTimeSec = 0;
    arrayOfPCB[index].currentWaitStartTimeNano = 0;
}

//modifies process control block values
void changePCB(int index, int cpuTime, int lastBurst, int priority, int preemptPercent, unsigned int sec, unsigned int ns)
{
    arrayOfPCB[index].totalCPUtime += cpuTime;
    arrayOfPCB[index].lastBurstTime = lastBurst;
    arrayOfPCB[index].processPriority = priority;
    arrayOfPCB[index].preemptPercentage = preemptPercent;
    arrayOfPCB[index].blockStartTimeSec = sec;
    arrayOfPCB[index].blockStartTimeNano = ns;
}

void clearPCB(int index)        //clears process control block values
{
    if(index >= 0)
    {
        arrayOfPCB[index].class = 99;
        arrayOfPCB[index].totalCPUtime = 0;
        arrayOfPCB[index].startTimeInSystemSec = 0;
        arrayOfPCB[index].startTimeInSystemNano = 0;
        arrayOfPCB[index].lastBurstTime = 0;
        arrayOfPCB[index].localPID = 0;
        arrayOfPCB[index].processPriority = 0;
        arrayOfPCB[index].preemptPercentage = 0;
        arrayOfPCB[index].blockStartTimeSec = 0;
        arrayOfPCB[index].blockStartTimeNano = 0;
        arrayOfPCB[index].currentWaitStartTimeSec = 0;
        arrayOfPCB[index].currentWaitStartTimeNano = 0;
    }
}

int receiveMessage()        //function to receive message from processes
{
    msgrcv(shmid2, &messageQueue, MESSAGESIZE, getpid(), 0);
    return messageQueue.mState;
}

void sendMessage(int type, int quantum)     //function to send message to process
{
    messageQueue.mState = 99;
    messageQueue.mType = type;
    messageQueue.mQuantum = quantum;

    msgsnd(shmid2, &messageQueue, MESSAGESIZE, 0);
}

int determineQuantum(int priority)  //determines quantum from priority
{
    return (QUANTUM / setPriorityMultiplier(priority));
}

int determineQuantumFromPercent(int priority, int percent)  //determines quantum use from a percent--for preempted processes
{
    int quantum = determineQuantum(priority);
    float p = (float)percent/100;

    quantum = (int)(p*quantum);
    return quantum;
}

void scheduleProcess(int pid, int priority, int resume) // function that schedules processes
{
    int quantum;

    if(resume == 1)
    {
        quantum = determineQuantum(priority);
        quantum = quantum - determineQuantumFromPercent(priority,arrayOfPCB[findKey(pid)].preemptPercentage);
    } else
    {
        quantum = determineQuantum(priority);
    }
    unsigned int tempTime = getRandomNumber(100,10000);
    incrementClock(0,tempTime);
    dispatchTimeOutput(tempTime);
    sendMessage(pid, quantum);
    int state = receiveMessage();
    while(state == 99)
    {
        state = receiveMessage();
    }
//    printf("Parent Rcv: %u %u\n", messageQueue.mState, messageQueue.mQuantum);

    unsigned int blockSec;
    unsigned int blockNS;

    switch(state)
    {
        case 0:     //terminated
            receiveOutput(pid, messageQueue.mQuantum*1000000, state);
            incrementClock(0,messageQueue.mQuantum*1000000);
            int element = findKey(pid);
            changePCB(element, messageQueue.mQuantum,(messageQueue.mQuantum),0,0,0,0);
            calculateStats(arrayOfPCB[element].startTimeInSystemSec,arrayOfPCB[element].startTimeInSystemNano,arrayOfPCB[element].totalCPUtime);
            clearPCB(element);
            processControlTable[element] = 0;
            preemptedProcess = 0;
            break;
        case 1:     //all quantum used
            receiveOutput(pid, messageQueue.mQuantum*1000000, state);
            incrementClock(0,messageQueue.mQuantum*1000000);
            if(priority > 0 && priority <= 2)
            {
                changePCB(findKey(pid), messageQueue.mQuantum,(messageQueue.mQuantum),priority+1,0,0,0);
                enqueueOutput(pid, priority+1, arrayOfPCB[findKey(pid)].class);
                enqueue(priority+1,pid);
            } else
            {
                changePCB(findKey(pid), messageQueue.mQuantum,(messageQueue.mQuantum),priority,0,0,0);
                enqueueOutput(pid, priority, arrayOfPCB[findKey(pid)].class);
                enqueue(priority,pid);
            }
            preemptedProcess = 0;
            break;
        case 2:     //blocked
            receiveOutput(pid, messageQueue.mQuantum*1000000, state);
            incrementClock(0,messageQueue.mQuantum*1000000);
            blockSec = *clockSec + getRandomNumber(0,3);
            blockNS = *clockNano  + getRandomNumber(0,1000);
            timesProcessesBlocked++;
            changePCB(findKey(pid), messageQueue.mQuantum,(messageQueue.mQuantum),1,0,*clockSec,*clockNano);
            enqueueBlock(pid, blockSec, blockNS);
            toBlockOutput(pid, blockSec, blockNS);
            preemptedProcess = 0;
            break;
        case 3:     //preempt
            quantum = determineQuantumFromPercent(priority,messageQueue.mQuantum);
            receiveOutput(pid, quantum*1000000, state);
            incrementClock(0,quantum*1000000);
            changePCB(findKey(pid), messageQueue.mQuantum,(messageQueue.mQuantum),priority,messageQueue.mQuantum,0,0);
            enqueueBlock(pid, 0, 0);
            preemptedProcess = 1;
            break;
        default:
            break;
    }
}

int runSceduler()       //main function of scheduling processes
{
    int pid;
    int priority;
    int repeatAgingCheck = 0;
    do{
        repeatAgingCheck = checkCurrentWaitTime();
    } while(repeatAgingCheck == 1);

    if(preemptedProcess == 0 && searchBlockQueueForReady() == 1)
    {
        if (blockQueue[0][1] == 0 && blockQueue[0][2] == 0)
        {
            pid = dequeueBlock();
            priority = arrayOfPCB[findKey(pid)].processPriority;
//            printf("Scheduling Preempted Process<%d>\n", pid);
            resumeOutput(pid);
            scheduleProcess(pid, priority, 1);
            return 1;
        } else {
            pid = dequeueBlock();
            priority = arrayOfPCB[findKey(pid)].processPriority;
//            printf("Removing Process From Block Queue<%d>\n", pid);
            unsigned int tempTime = getRandomNumber(200,15000);
            incrementClock(0,tempTime);
            fromBlockOutput(pid, priority, tempTime);
            arrayOfPCB[findKey(pid)].processPriority = 1;
            enqueue(1, pid);
        }
    } else {
        priority = findNextInQueue();
        if(priority >= 0)
        {
            pid = dequeue(priority);
//            printf("Scheduling Process <%d>\n",pid);
            schedProcOutput(pid,priority,arrayOfPCB[findKey(pid)].class);
            scheduleProcess(pid,priority,0);
            return 1;
        }
    }

    return 0;
}

int processLaunchCheck() //checks if program is under limit of max processes so program can generate more--returns 1 if good / returns 0 if not ok
{
    if(activeChildren < ACTIVEPROCESSLIMIT)
    {
        if(*clockSec > nextProcessLaunchTimeSec)
        {
            return 1;
        } else if(*clockSec == nextProcessLaunchTimeSec && *clockNano >= nextProcessLaunchTimeNSec)
        {
            return 1;
        } else return 0;
    } else return 0;
}

void parentCleanup()            //function to cleanup shared memory and close output file
{
    fclose(output);                                                     //close file output
    shmdt(clockSim);                                                    //detach from shared memory
    shmdt(arrayOfPCB);                                                  //detach from shared memory
    shmctl(shmid0,IPC_RMID,NULL);                                       //remove shared memory segment
    shmctl(shmid1,IPC_RMID,NULL);                                       //remove shared memory segment
    msgctl(shmid2, IPC_RMID, NULL);                                     //remove shared memory segment
}

void getNextProcessTime()       //calculates a time in the future to generate a new process
{
    unsigned int nextSec = getRandomNumber(0,MAXTIMEBETWEENNEWPROCSSECS);
    unsigned int nextNSec = getRandomNumber(0,MAXTIMEBETWEENNEWPROCSNS);
    unsigned int currentSec = clockSim[0];
    unsigned int currentNSec = clockSim[1];

    nextProcessLaunchTimeSec = currentSec + nextSec;
    nextProcessLaunchTimeNSec = currentNSec + nextNSec;

    while((unsigned int)nextProcessLaunchTimeNSec >= 1000000000)                      //if nano seconds are equal or greater than 1 second
    {
        nextProcessLaunchTimeSec = (unsigned int)nextProcessLaunchTimeSec + 1;                        //increase seconds by 1
        nextProcessLaunchTimeNSec = (unsigned int)nextProcessLaunchTimeNSec - 1000000000;               //decrease nano seconds by 1 second
    }
}

int checkIfUserProcess()        //determines if process will be a realtime or user process   realtime=0  user=1
{
    int randomNumber = getRandomNumber(1,100);
    if(randomNumber > 0 && randomNumber <= PROBOFUSERPROCESS)
    {
        return 1;
    } else
    {
        return 0;
    }
}

void handle_sigint()                    //signal handler for interrupt signal(Ctrl-C)
{
    printf("\nProgram aborted by user\n");        //displays end of program message to user
    parentCleanup();
    kill(0,SIGTERM);
    while (waitpid(-1, NULL, WNOHANG) > 0);                             //wait until all children have terminated
    while (activeChildren > 0);

    exit(1);
}

void handle_sigalarm()          //signal handler for alarm signal(for time out condition)
{
    printf("\nTimeout - Ending program\n");       //displays end of program message to user
    parentCleanup();
    kill(0,SIGTERM);
    while (waitpid(-1, NULL, WNOHANG) > 0);                             //wait until all children have terminated
    while (activeChildren > 0);

    exit(1);
}

void handle_sigchild()      //signal handler for child termination
{
    while (waitpid(-1, NULL, WNOHANG) > 0)          //handle each child that has terminated
    {
        activeChildren--;                           //decrement number of active children
        processesTerminatedSoFar++;
    }
}


int main(int argc, char *argv[])
{
    printf("Program running...\n");
    signal(SIGINT, handle_sigint);          //initialization of signals and which handler will be used for each
    signal(SIGALRM, handle_sigalarm);
    alarm(3);                               //will send alarm signal after 3 seconds
    signal(SIGCHLD, handle_sigchild);

    strcpy(programName,argv[0]);
    strcat(programName,":");
    makeNewOutputFile();

    srand(time(0));
    seed = rand();

    opterr = 0;                             //disables some system error messages(using custom error messages so this is not needed)

    shmid0 = shmget(SHMKEY, 2*SIZEOFINT, 0600 | IPC_CREAT);     //creates shared memory id for the clock simulator
    if (shmid0 == -1)
    {
        perror("Shared memory0P:");                           //if shared memory does not exist print error message and exit
        return 1;
    }
    clockSim = (int*)shmat(shmid0, 0, 0);                  //attaches to the shared memory clock simulator

    shmid1 = shmget(SHMKEY+1, ACTIVEPROCESSLIMIT * PCBSIZE, 0600 | IPC_CREAT);  //creates shared memory id for the process control block
    if (shmid1 == -1)
    {
        perror("Shared memory1P:");                           //if shared memory does not exist print error message and exit
        return 1;
    }
    arrayOfPCB = (pcb *)shmat(shmid1, 0, 0);                //attaches to shared memory array of process control blocks

    int i;
    for(i=0;i<ACTIVEPROCESSLIMIT;i++)
    {
        arrayOfPCB[i].class = 99;
    }

    shmid2 = msgget(SHMKEY+2, 0600 | IPC_CREAT);                //creates shared memory id for messaging queue
    if (shmid2 == -1)
    {
        perror("Shared memory2P:");                           //if shared memory does not exist print error message and exit
        return 1;
    }

    clockSim[0] = (unsigned int)0;                          //initializing clock simulator
    clockSim[1] = (unsigned int)0;
    clockSec = &clockSim[0];                                 //creates a pointer to the clock simulator to be used in the increment clock function
    clockNano = &clockSim[1];
    int pid;
    int launchprocess=0;
    int scheduledProcess=0;

    getNextProcessTime();

    while(totalProcessesLaunched < TOTALPROCESSESLIMIT)         //loop that repeats until all processes have been generated
    {
        launchprocess = processLaunchCheck();

        if(launchprocess == 1)
        {
            int priority;
            activeChildren++;
            pid = fork();                                   //fork call
            if(pid < 0)
            {
                fprintf(stderr, "Fork failed:%s\n",strerror(errno));
                parentCleanup();
                kill(0,SIGTERM);
                while (waitpid(-1, NULL, WNOHANG) > 0);                             //wait until all children have terminated
                while (activeChildren > 0);

                exit(-1);
            }
            if (pid == 0)
            {
                execl("process", NULL);                 //exec call
            }

//            printf("Launching pid: %d\n",pid);
            int userProcess = checkIfUserProcess();

            for(i=0;i<ACTIVEPROCESSLIMIT;i++)       //loop for locating an open position in the process control table
            {
                if(processControlTable[i] == 0)       //finds empty spot in table
                {
                    processControlTable[i] = pid;
                    if(userProcess == 0)       //initialize real time process
                    {
                        priority = 0;
                        initializePCB(i, 0, 0, clockSim[0], clockSim[1], 0, totalProcessesLaunched, priority);
                        genProcOutput(pid,priority,userProcess);
                        enqueue(0,pid);
                    } else {                            //initialize user process
                        priority = getRandomNumber(1,3);
                        initializePCB(i, 1, 0, clockSim[0], clockSim[1], 0, totalProcessesLaunched, priority);
                        genProcOutput(pid,priority,userProcess);
                        enqueue(priority,pid);
                    }
                    break;
                }
            }
            getNextProcessTime();
            totalProcessesLaunched++;
        }

//        printf("PostLaunch Tot proc:%d  Active proc:%d\n",totalProcessesLaunched,activeChildren);
        scheduledProcess = runSceduler();
//        printClock();
//        displayAll();
//        printf("\n");
        if(scheduledProcess == 0)
        {
            addCPUIdleTime(1,getRandomNumber(0,1000000000));
            preemptedProcess = 0;
        }
    }

    while(activeChildren > 0)   //loop to finish processes and make sure all active child processes have been terminated
    {
//        printf("Cleanup Tot proc:%d  Active proc:%d\n",totalProcessesLaunched,activeChildren);
        scheduledProcess = runSceduler();
//        printClock();
//        displayAll();
//        printf("\n");
        if(scheduledProcess == 0)
        {
            addCPUIdleTime(1,getRandomNumber(0,1000000000));
            preemptedProcess = 0;
        }
    }

    printClockOutput();
    displayStats();

    parentCleanup();
    printf("Program completed successfully! Number of log files:%d\n",logFileCount);
    return 0;
}