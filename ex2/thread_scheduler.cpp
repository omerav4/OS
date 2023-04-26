#include "thread_scheduler.h"
#include <sys/time.h>

#define TO_SEC 1000000
#define FAIL -1
#define SUCCESS 0
#define MAIN_THREAD_ID 0
#define FROM_LONGJMP 3
#define FALSE -1

#define ERROR_MESSAGE_SETTIMER_ERROR "system error: settimer failed\n"

ThreadsScheduler::ThreadsScheduler(int quantum_usecs){
    readyThreads = new std::queue<Thread*>();
    blockedThreads = new std::queue<Thread*>();
    sleepingThreads = new std::queue<SleepingThread*>();
    running = nullptr;
    quantumCounter = 0;

    for (int i = 0; i < MAX_THREAD_NUM; i++){
        allThreads[i] = nullptr;
    }
}

ThreadsScheduler::~ThreadsScheduler(){

}

int ThreadsScheduler::getNextAvailableId(){
    for (int i = 1; i < MAX_THREAD_NUM; i++){
        if (allThreads[i] == nullptr){
            return i;
        }
    }
    return FAIL;
}

void ThreadsScheduler::addNewThread(Thread *thread, int tid){
    allThreads[tid] = thread;
}

void ThreadsScheduler::setRunningThread(Thread *thread){
    thread->setState(RUNNING);
    running = thread;
}

void ThreadsScheduler::addReadyThread(Thread *thread){
    thread->setState(READY);
    readyThreads->push(thread);
}

void ThreadsScheduler::addBlockedThread(Thread *thread){
    thread->setState(BLOCKED);
    blockedThreads->push(thread);
}

int ThreadsScheduler::isTidExist(int tid){
    if (tid < 0 || tid >= MAX_THREAD_NUM || allThreads[tid] == nullptr){
        return FAIL;
    }
    return SUCCESS;
}

Thread* ThreadsScheduler::getThread(int tid){
    return allThreads[tid];
}
void ThreadsScheduler::setNextRunningThread(int isCurrentThreadSleeping){
    Thread *nextThread;
    nextThread = readyThreads->front();
    if (nextThread == nullptr){
        //change to main thread? supposed to be the running thread
        nextThread = running;
    }
    else{
        readyThreads->pop();
        if(isCurrentThreadSleeping == FALSE){
            addReadyThread(running);
        }
    }
    running = nextThread;
    running->setState(RUNNING);
    increaseQuantum();
    siglongjmp(running->env, FROM_LONGJMP);
}

void ThreadsScheduler::deleteReadyThread(Thread *thread){
    std::queue<Thread*> *helperQueue;
    helperQueue = new std::queue<Thread*>();
    Thread *helperThread;

    while(!readyThreads->empty()){
        helperThread = readyThreads->front();
        if(helperThread != thread){
            helperQueue->push(helperThread);
        }
        readyThreads->pop();
    }
    readyThreads = helperQueue;
}

void ThreadsScheduler::deleteBlockedThread(Thread *thread){
    std::queue<Thread*> *helperQueue;
    helperQueue = new std::queue<Thread*>();
    Thread *helperThread;

    while(!blockedThreads->empty()){
        helperThread = blockedThreads->front();
        if(helperThread != thread){
            helperQueue->push(helperThread);
        }
        blockedThreads->pop();
    }
    readyThreads = helperQueue;
}

void ThreadsScheduler::deleteThreadTid(int tid) {
    allThreads[tid] = nullptr;
}

sigset_t* ThreadsScheduler::getSignalsSet(){
    return &signals_set;
}

int ThreadsScheduler::getRunningThreadTid(){
    return running->getId();
}

Thread* ThreadsScheduler::getRunningThread(){
    return running;
}

int ThreadsScheduler::getTotalQuantums(){
    return quantumCounter;
}

void ThreadsScheduler::addSleepingThread(Thread *thread, int num_quantums){
    SleepingThread *newThread;
    newThread->thread = thread;
    newThread->count = num_quantums;
    sleepingThreads->push(newThread);
}

void ThreadsScheduler::increaseQuantum(){
    printf("quantum counter %d", quantumCounter);
    quantumCounter += 1;
    updateSleepingThreads();
}

void ThreadsScheduler::updateSleepingThreads(){
    std::queue<SleepingThread*> *helper;
    helper = new std::queue<SleepingThread*>();

    SleepingThread *sleepingThread;
    while(!sleepingThreads->empty()){
        sleepingThread = sleepingThreads->front();
        sleepingThread->count -= 1;
        if (sleepingThread->count == 0){
            addReadyThread(sleepingThread->thread);
        }
        else{
            helper->push(sleepingThread);
        }
        sleepingThreads->pop();
    }
    sleepingThreads = helper;
}

itimerval ThreadsScheduler::getVirtualTimer(){
    return timer;
}
