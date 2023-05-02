#include "thread_scheduler.h"
#include <sys/time.h>

#define TO_SEC 1000000
#define FAIL -1
#define SUCCESS 0
#define MAIN_THREAD_ID 0
#define FROM_LONGJMP 1
#define FALSE -1

#define ERROR_MESSAGE_SETTIMER_ERROR "system error: settimer failed\n"
int ThreadsScheduler::quantumCounter = 1;

ThreadsScheduler::ThreadsScheduler(){
    readyThreads = new std::queue<Thread*>();
    blockedThreads = new std::queue<Thread*>();
    sleepingThreads = new std::queue<Thread*>();
    running = nullptr;

    for (int i = 0; i < MAX_THREAD_NUM; i++){
        allThreads[i] = nullptr;
    }
}

ThreadsScheduler::~ThreadsScheduler(){
}

int ThreadsScheduler::getNextAvailableId(){
    //find the next minimum available tid
    for (int i = 1; i < MAX_THREAD_NUM; i++){
        if (allThreads[i] == nullptr){
            return i;
        }
    }
    return FAIL;
}

void ThreadsScheduler::addNewThread(Thread *thread, unsigned int tid){
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

int ThreadsScheduler::isTidExist(unsigned int tid){
    if (tid >= MAX_THREAD_NUM || allThreads[tid] == nullptr || tid < 0){
        return FAIL;
    }
    return SUCCESS;
}

Thread* ThreadsScheduler::getThread(unsigned int tid){
    return allThreads[tid];
}
void ThreadsScheduler::setNextRunningThread(){
    Thread* next_thread = readyThreads->front();    // get the next ready thread

    //if the ready threads queue is empty, we will continue with the current thread
    if (next_thread == nullptr){
        next_thread = running;
    }
    next_thread->setState(RUNNING);
    running = next_thread;
    readyThreads->pop();
    next_thread->increase_running_quantum();
    quantumCounter++;   // update the total quantum counter after we change the running thread
    // siglongjmp(running->env, FROM_LONGJMP);

}

void ThreadsScheduler::deleteReadyThread(Thread *thread){
    // creates a helper queue, and iterates the ready threads queue until we will find the given thread, and we will
    // not add it to the helper queue
    std::queue<Thread*> *helper_queue;
    helper_queue = new std::queue<Thread*>();
    Thread *helperThread;

    while(!readyThreads->empty()){
        helperThread = readyThreads->front();
        if(helperThread != thread){
            helper_queue->push(helperThread);
        }
        readyThreads->pop();
    }
    readyThreads = helper_queue;
}

void ThreadsScheduler::deleteBlockedThread(Thread *thread){
    // creates a helper queue, and iterates the blocked threads queue until we will find the given thread, and we will
    // not add it to the helper queue
    std::queue<Thread*> *helper_queue;
    helper_queue = new std::queue<Thread*>();
    Thread *helperThread;

    while(!blockedThreads->empty()){
        helperThread = blockedThreads->front();
        if(helperThread != thread){
            helper_queue->push(helperThread);
        }
        blockedThreads->pop();
    }
    readyThreads = helper_queue;
}

void ThreadsScheduler::deleteThreadTid(unsigned int tid) {
    // change the pointer in the allThreads array of the given tid to null and free the resources of that thread
    delete allThreads[tid];
    allThreads[tid] = nullptr;
}

sigset_t* ThreadsScheduler::getSignalsSet(){
    return &signals_set;
}

unsigned int ThreadsScheduler::getRunningThreadTid(){
    return running->getId();
}

Thread* ThreadsScheduler::getRunningThread(){
    return running;
}

int ThreadsScheduler::getTotalQuantums(){
    return quantumCounter;
}

void ThreadsScheduler::addSleepingThread(Thread *thread, int num_quantums){
    thread->set_sleep_quantums(num_quantums);
    sleepingThreads->push(thread);
}

void ThreadsScheduler::updateSleepingThreads(){
    // creates a helper queue, and iterates the sleeping threads queue until we will find the given thread, update their
    // sleeping counter, and if there is an unblocked tread with sleeping counter that equals to 0, adds it to the ready
    // threads queue
    std::queue<Thread*> *helper_queue;
    helper_queue = new std::queue<Thread*>();

    Thread *sleepingThread;
    while(!sleepingThreads->empty()){
        sleepingThread = sleepingThreads->front();
        sleepingThread->set_sleep_quantums(sleepingThread->getSleepingQuantums() - 1);
        if ((sleepingThread->getSleepingQuantums() == 0) && sleepingThread->getState() != BLOCKED){
            addReadyThread(sleepingThread);
        }
        else{
            helper_queue->push(sleepingThread);
        }
        sleepingThreads->pop();

    }
    sleepingThreads = helper_queue;
}
