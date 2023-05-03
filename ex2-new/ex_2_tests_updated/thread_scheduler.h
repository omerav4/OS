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
    Thread *all_threads[MAX_THREAD_NUM];
    std::queue<Thread*> *ready_threads;
    std::queue<Thread*> *blocked_threads;
    std::queue<Thread*> *sleeping_threads;
    Thread *running;
    sigset_t signals_set;

public:
    static int quantum_counter;
    ThreadsScheduler();
    ~ThreadsScheduler();
    void free_resources();

    int get_next_available_id();
    void add_new_thread(Thread *thread, unsigned int tid);
    void set_running_thread(Thread *thread);
    void add_ready_thread(Thread *thread);
    void add_blocked_thread(Thread *thread);
    int is_tid_exist(unsigned int tid);
    Thread* get_thread(unsigned int tid);
    void set_next_running_thread();
    void delete_ready_thread(Thread *thread);
    void delete_blocked_thread(Thread *thread);
    void delete_thread_tid(unsigned int tid);
    sigset_t* get_signals_set();
    Thread* get_running_thread();
    unsigned int get_running_thread_tid();
    int get_total_quantums();
    void add_sleeping_thread(Thread *thread, int num_quantums);
    void update_sleeping_threads();
};

#endif
