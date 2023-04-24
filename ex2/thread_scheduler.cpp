#include "thread_scheduler.h"
#include <sys/time.h>

#define TO_SEC 1000000
#define FAIL -1
#define SUCCESS 0
#define MAIN_THREAD_ID 0


ThreadsScheduler::ThreadsScheduler(int quantum_usecs){
    readyThreads = new std::queue<Thread*>();
    blockedThreads = new std::queue<Thread*>();
    sleepingThreads = new std::queue<SleepingThread*>();
    running = nullptr;
    quantumCounter = 0;

    for (int i = 0; i < MAX_THREAD_NUM; i++){
        allThreads[i] = nullptr;
    }

    // configures the timer - verify that it's valid!!!
    timer.it_value.tv_sec = quantum_usecs / TO_SEC;
    timer.it_value.tv_usec = quantum_usecs % TO_SEC;
    timer.it_interval.tv_sec = quantum_usecs / TO_SEC;
    timer.it_interval.tv_usec = quantum_usecs % TO_SEC;
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
    std::cout << "tid: " << tid << std::endl;
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
void ThreadsScheduler::setRunningThread(){
    readyThreads->pop();
    running = readyThreads->front();
    increaseQuantum();
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

itimerval* ThreadsScheduler::getVirtualTimer(){
    return &timer;
}
