#include "thread.h"
#include "uthreads.h"
#include <iostream>
#include <queue>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>

#ifndef THREAD_SCHEDULER_H
#define THREAD_SCHEDULER_H

class ThreadsScheduler{
private:
    Thread *allThreads[MAX_THREAD_NUM];
    std::queue<Thread*> *readyThreads;
    std::queue<Thread*> *blockedThreads;
    std::queue<Thread*> *sleepingThreads;
    Thread *running;
    sigset_t signals_set;

public:
    static int quantumCounter;
    ThreadsScheduler();
    ~ThreadsScheduler();

    int getNextAvailableId();
    void addNewThread(Thread *thread, unsigned int tid);
    void setRunningThread(Thread *thread);
    void addReadyThread(Thread *thread);
    void addBlockedThread(Thread *thread);
    int isTidExist(unsigned int tid);
    Thread* getThread(unsigned int tid);
    void setNextRunningThread();
    void deleteReadyThread(Thread *thread);
    void deleteBlockedThread(Thread *thread);
    void deleteThreadTid(unsigned int tid);
    sigset_t* getSignalsSet();
    Thread* getRunningThread();
    unsigned int getRunningThreadTid();
    int getTotalQuantums();
    void addSleepingThread(Thread *thread, int num_quantums);
    void updateSleepingThreads();
};


#endif
