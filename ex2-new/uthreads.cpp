#include <signal.h>
#include "uthreads.h"
#include "thread_scheduler.h"
#include "thread.h"
#include <iostream>
#include <sys/time.h>

///------------------------------------------- constants-------------------------------------------------------
#define MAIN_THREAD_ID 0
#define FAIL -1
#define SUCCESS 0
#define FROM_LONGJMP 0
#define TRUE 1
#define FALSE -1

#define ERROR_MESSAGE_QUANTUM_USECS_NON_POSITIVE "thread library error: quantum_usecs is non-positive\n"
#define ERROR_MESSAGE_SIGACTION_ERROR "system error: sigaction failed\n"
#define ERROR_MESSAGE_NO_AVAILABLE_ID "thread library error: no available id for new thread (above the maximum) \n"
#define ERROR_MESSAGE_ALLOCATION_FAILURE "system error: allocation failure\n"
#define ERROR_MESSAGE_NULL_ENTRY_POINT "thread library error: entry_point is null\n"
#define ERROR_MESSAGE_TID_NOT_EXISTS "thread library error: given tid does not exist\n"
#define ERROR_MESSAGE_SIGPROMASK_ERROR "system error: sigprocmask failed\n"
#define ERROR_MESSAGE_MAIN_THREAD_CANT_SLEEP "thread library error: the main thread can't sleep\n"
#define ERROR_MESSAGE_SETTIMER_ERROR "system error: settimer failed\n"
#define ERROR_MESSAGE_TERMINATE_MAIN_THREAD "thread library error: terminates main thread\n"

#define TO_SEC 1000000
#define STACK_SIZE 4096

///------------------------------------------- global variables -------------------------------------------------------
struct itimerval timer;
int quantum;
enum Signals {
    SIGSLEEP = SIGVTALRM + 1,
    SIGTERMINATE = SIGVTALRM + 2
};
ThreadsScheduler* scheduler;
struct sigaction sa = {0};

///-------------------------------------- helper functions ------------------------------------------------

/**
 * Blocks the current set of signals
 */
void block_signals_set(){
    sigprocmask(SIG_BLOCK, scheduler->get_signals_set(), nullptr);
}

/**
 * Unblocks the current set of signals
 */
void unblock_signals_set(){
    sigprocmask(SIG_UNBLOCK, scheduler->get_signals_set(), nullptr);
}

/**
 * configures the timer with the given quantum_usecs
 * @param quantum_usecs- the length of a quantum in micro-seconds
 */
void configure_timer(int quantum_usecs){
    scheduler->update_sleeping_threads();
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0)
    {
        std::cerr << ERROR_MESSAGE_SIGACTION_ERROR << std::endl;
        delete scheduler;
        exit(EXIT_FAILURE);
    }

    // TODO check if it's correct
//    timer.it_value.tv_sec = quantum_usecs;
//    timer.it_value.tv_usec = 0;
//    timer.it_interval.tv_sec = 0;
//    timer.it_interval.tv_usec = 0;

    timer.it_value.tv_sec = quantum_usecs / TO_SEC;        // first time interval, seconds part
    timer.it_value.tv_usec = 0;        // first time interval, microseconds part

    // configure the timer to expire every quantum.
    timer.it_interval.tv_sec = 0;    // following time intervals, seconds part
    timer.it_interval.tv_usec = 0;    // following time intervals, microseconds part

    // starts a virtual timer, it counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
        std::cerr << ERROR_MESSAGE_SETTIMER_ERROR << std::endl;
        delete scheduler;
        exit(EXIT_FAILURE);
    }
}

/**
 * handlar function that will handle with each possible signla, according to the given signal number
 * @param signal- the number of the signal
 */
void signals_handler(int signal)
{
    block_signals_set();
    Thread* running = scheduler->get_running_thread();
    switch (signal) {
        case SIGVTALRM: // if the timer expires, add the current thread to the ready threads list
            running->set_state(READY);
            scheduler->add_ready_thread(running);
            break;

        case SIGSLEEP: // if the thread blocked or sleeping, add the thread to the sleeping threads list
            scheduler->add_sleeping_thread(running, running->get_sleeping_quantums());
            break;

        case SIGTERMINATE: // if the thread terminates, change the running pointer to nullptr without add the current
        // running pointer to the ready threads list
            running = nullptr;
            break;
    }
    if(running == nullptr){             // if the thread terminates
        scheduler->set_next_running_thread();
        running = scheduler->get_running_thread();
    }
    else{
        if (sigsetjmp(running->env, 1) == SUCCESS){
            scheduler->set_next_running_thread();
            running = scheduler->get_running_thread();
        }
        else{
            return;
        }
    }
    unblock_signals_set();
    configure_timer(quantum);
    siglongjmp(running->env, 1);
}

/**
 * Creates the main thread by the default ctor for the thread class, sets it as the running thread and adds it
 * to the array of the threads
 */
void create_main_thread(){
    Thread* new_thread = new(std::nothrow) Thread();           // there is a default ctor for the main thread
    if(new_thread == nullptr){
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        delete scheduler;
        exit(EXIT_FAILURE);
    }
    scheduler->add_new_thread(new_thread, MAIN_THREAD_ID);
    scheduler->set_running_thread(new_thread);
}

///-------------------------------------- library functions ------------------------------------------------
int uthread_init(int quantum_usecs) {
    // checks if quantum_usecs is non-positive
    if (quantum_usecs <= 0) {
        std::cerr << ERROR_MESSAGE_QUANTUM_USECS_NON_POSITIVE << std::endl;
        return FAIL;
    }

    // creates the scheduler and the main thread, and configures the timer
    scheduler = new(std::nothrow) ThreadsScheduler();
    if(scheduler == nullptr){
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    create_main_thread();
    quantum = quantum_usecs;
    configure_timer(quantum);

    // installs the signal handler
    sa.sa_handler = &signals_handler;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0) {
        std::cerr << ERROR_MESSAGE_SIGACTION_ERROR << std::endl;
        delete scheduler;
        exit(EXIT_FAILURE);
    }
    return SUCCESS;
}

int uthread_spawn(thread_entry_point entry_point) {
    // checks if entry_point is null
    if (entry_point == nullptr){
        std::cerr << ERROR_MESSAGE_NULL_ENTRY_POINT << std::endl;
        return FAIL;
    }

    // gets the next available id
    int id = scheduler->get_next_available_id();
    if (id == FAIL) {
        std::cerr << ERROR_MESSAGE_NO_AVAILABLE_ID << std::endl;
        return FAIL;
    }

    block_signals_set();
    // creates a new stack
    char* stack = new(std::nothrow) char[STACK_SIZE];
    if (stack == nullptr) {
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        return FAIL;
    }

    // creates the new thread
    Thread* new_thread = new(std::nothrow) Thread(id, stack, entry_point);
    if (new_thread == nullptr) {
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        return FAIL;
    }

    // updates the scheduler with the new thread
    scheduler->add_new_thread(new_thread, id);
    scheduler->add_ready_thread(new_thread);

    unblock_signals_set();
    return id;
}

int uthread_terminate(int tid) {

    // checks if the tid is valid
    if(scheduler->is_tid_exist(tid) == FAIL) {
        std::cerr << ERROR_MESSAGE_TID_NOT_EXISTS << std::endl;
        return FAIL;
    }

    block_signals_set();

    // if the given thread is the main thread
    if (tid == MAIN_THREAD_ID){
        delete scheduler;
        exit(EXIT_SUCCESS);
    }
    Thread* thread = scheduler->get_thread(tid);
    ThreadState state = thread->get_state();

    // if a running thread terminates itself
    if (state == RUNNING){
        scheduler->delete_thread_tid(tid);
        unblock_signals_set();
        signals_handler(SIGTERMINATE);
    }
    else if (state == READY){
        scheduler->delete_ready_thread(thread);
        scheduler->delete_thread_tid(tid);
        unblock_signals_set();
        return EXIT_SUCCESS;
    }
    else if (state == BLOCKED){
        scheduler->delete_blocked_thread(thread);
        scheduler->delete_thread_tid(tid);
        unblock_signals_set();
        return EXIT_SUCCESS;
    }
    unblock_signals_set();
    return EXIT_FAILURE;
}

int uthread_block(int tid) {
    // checks if the tid is valid
    if(scheduler->is_tid_exist(tid) == FAIL || tid == MAIN_THREAD_ID) {
        std::cerr << ERROR_MESSAGE_TID_NOT_EXISTS << std::endl;
        return FAIL;
    }

    block_signals_set();

    // gets the current thread and handles it according to it's state
    Thread* thread = scheduler->get_thread(tid);
    ThreadState state = thread->get_state();
    if (state == RUNNING) {
        thread->set_state(BLOCKED);
        signals_handler(SIGSLEEP);
    }
    else if (state == READY) {
        scheduler->delete_ready_thread(thread);
        thread->set_state(BLOCKED);
        scheduler->add_sleeping_thread(thread, 0);
    }
    unblock_signals_set();
    return SUCCESS;
}

int uthread_resume(int tid) {
    // checks if tid exists
    if (scheduler->is_tid_exist(tid) == FAIL) {
        std::cerr << ERROR_MESSAGE_TID_NOT_EXISTS << std::endl;
        return FAIL;
    }
    block_signals_set();

    // gets the current thread, removes it from the blocked threads queue and adds it to the ready threads queue
    // after change its state
    Thread* thread = scheduler->get_thread(tid);
    ThreadState state = thread->get_state();

    if(state == BLOCKED){
        thread->set_state(READY);
    }
    unblock_signals_set();
    return SUCCESS;

}

int uthread_sleep(int num_quantums){
    int tid = uthread_get_tid();
    Thread* running = scheduler->get_thread(tid);

    // checks if the running thread is the main thread
    if (tid == MAIN_THREAD_ID){
        std::cerr << ERROR_MESSAGE_MAIN_THREAD_CANT_SLEEP << std::endl;
        return FAIL;
    }

    block_signals_set();

    // changes the state of the running thread, updates it's sleeping counter and handles it as a unique signal
    running->set_state(READY);
    running->set_sleep_quantums(num_quantums);
    unblock_signals_set();

    signals_handler(SIGSLEEP);
    return SUCCESS;
}

int uthread_get_tid(){
    block_signals_set();
    int tid = scheduler->get_running_thread_tid();
    unblock_signals_set();
    return tid;
}

int uthread_get_total_quantums(){
    block_signals_set();
    int quantums = scheduler->get_total_quantums();
    unblock_signals_set();
    return quantums;
}

int uthread_get_quantums(int tid){
    block_signals_set();
    if(scheduler->is_tid_exist(tid) == FAIL) {
        std::cerr << ERROR_MESSAGE_TID_NOT_EXISTS << std::endl;
        unblock_signals_set();
        return FAIL;
    }
    int quantums = scheduler->get_thread(tid)->get_running_quantums();
    unblock_signals_set();
    return quantums;
}