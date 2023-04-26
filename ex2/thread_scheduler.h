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

    struct SleepingThread{
        Thread* thread;
        int count;
    };
    std::queue<SleepingThread*> *sleepingThreads;
    Thread *running;
    int quantumCounter;
    sigset_t signals_set;
    struct itimerval timer;

public:
    ThreadsScheduler(int quantum_usecs);
    ~ThreadsScheduler();

    int getNextAvailableId();
    void addNewThread(Thread *thread, int tid);
    void setRunningThread(Thread *thread);
    void addReadyThread(Thread *thread);
    void addBlockedThread(Thread *thread);
    int isTidExist(int tid);
    Thread* getThread(int tid);
    void setNextRunningThread(int isCurrentThreadSleeping);
    void deleteReadyThread(Thread *thread);
    void deleteBlockedThread(Thread *thread);
    void deleteThreadTid(int tid);
    sigset_t* getSignalsSet();
    Thread* getRunningThread();
    int getRunningThreadTid();
    int getTotalQuantums();
    void addSleepingThread(Thread *thread, int num_quantums);
    void increaseQuantum();
    void updateSleepingThreads();
    itimerval* getVirtualTimer();
};


#endif
