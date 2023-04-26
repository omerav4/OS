#include <signal.h>
#include "uthreads.h"
#include "thread_scheduler.h"
#include "thread.h"
#include <iostream>
#include <sys/time.h>

#define MAIN_THREAD_ID 0
#define FAIL -1
#define SUCCESS 0
#define FROM_LONGJMP 0
#define TRUE 1
#define FALSE -1

#define ERROR_MESSAGE_QUANTUM_USECS_NON_POSITIVE "thread library error: quantum_usecs is non-positive\n"
#define ERROR_MESSAGE_SIGACTION_ERROR "system error: sigaction failed\n"
#define ERROR_MESSAGE_NO_AVAILABLE_ID "thread library error: no available id for new thread\n"
#define ERROR_MESSAGE_CANT_ALLOCATE_STACK "system error: can't allocate space for the stack\n"
#define ERROR_MESSAGE_NULL_ENTRY_POINT "thread library error: entry_point is null\n"
#define ERROR_MESSAGE_TID_NOT_EXISTS "thread library error: given tid does not exist\n"
#define ERROR_MESSAGE_SIGPROMASK_ERROR "system error: sigprocmask failed\n"
#define ERROR_MESSAGE_MAIN_THREAD_CANT_SLEEP "thread library error: the main thread can't sleep\n"
#define ERROR_MESSAGE_SETTIMER_ERROR "system error: settimer failed\n"

#define TO_SEC 1000000
#define STACK_SIZE 4096


#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
            : "=g" (ret)
            : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}
#endif

ThreadsScheduler *scheduler;
struct sigaction sa = {0};

void block_signals_set(){
    if (sigprocmask(SIG_BLOCK, scheduler->getSignalsSet(), nullptr) == FAIL)
    {
        std::cerr << ERROR_MESSAGE_SIGPROMASK_ERROR << std::endl;
        delete scheduler;
        exit(EXIT_FAILURE);
    }
}

void unblock_signals_set(){
    if (sigprocmask(SIG_UNBLOCK, scheduler->getSignalsSet(), nullptr) == FAIL)
    {
        std::cerr << ERROR_MESSAGE_SIGPROMASK_ERROR << std::endl;
        delete scheduler;
        exit(EXIT_FAILURE);
    }
}

void signal_handler(int sigNum){
    if (sigNum != SIGVTALRM){
        return;
    }

    block_signals_set();
    Thread* running = scheduler->getRunningThread();
    int setjmp_val = sigsetjmp(running->env, 1);
    if (setjmp_val != FROM_LONGJMP)
    {
        if (running->getState() != BLOCKED)
        {
            running->setState(READY);
            scheduler->addReadyThread(running);
        }
        scheduler->setNextRunningThread(FALSE);
    }
    unblock_signals_set();
}

void timer_handler(int sig)
{
    block_signals_set();
    //scheduler->increaseQuantum();
    scheduler->setNextRunningThread(FALSE);
    unblock_signals_set();
}

void configure_timer(int quantum_usecs){
    struct itimerval timer;
    sa.sa_handler = &timer_handler;
    timer = scheduler->getVirtualTimer();
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
        std::cerr << ERROR_MESSAGE_SIGACTION_ERROR << std::endl;
    }
    timer.it_value.tv_sec = quantum_usecs / TO_SEC;
    timer.it_value.tv_usec = quantum_usecs % TO_SEC;
    timer.it_interval.tv_sec = quantum_usecs / TO_SEC;
    timer.it_interval.tv_usec = quantum_usecs % TO_SEC;

    // starts a virtual timer. it counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
        std::cerr << ERROR_MESSAGE_SETTIMER_ERROR << std::endl;
//        ~ThreadsScheduler();
        exit(EXIT_FAILURE);
    }
}

void create_main_thread(){
    char *stack;
    address_t sp;
    Thread *newThread;

    // creates a new stack
    stack = new(std::nothrow) char[STACK_SIZE];
    if (stack == nullptr) {
        std::cerr << ERROR_MESSAGE_CANT_ALLOCATE_STACK << std::endl;
    }
    sp = (address_t) stack + STACK_SIZE - sizeof(address_t);

    // creates the new thread
    newThread = new Thread(MAIN_THREAD_ID, stack, sp);

    // initializes the env of the new thread to use the right stack, and to run from the function 'entry_point',
    // when we'll use siglongjmp to jump into the thread.
    sigsetjmp(newThread->env, 1);
    ((newThread->env)->__jmpbuf)[JB_SP] = translate_address(sp);
    //((newThread->env)->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&(newThread->env)->__saved_mask);

    scheduler->addNewThread(newThread, MAIN_THREAD_ID);
    scheduler->setRunningThread(newThread);
}

int uthread_init(int quantum_usecs) {
    // checks if quantum_usecs is non-positive
    if (quantum_usecs <= 0) {
        std::cerr << ERROR_MESSAGE_QUANTUM_USECS_NON_POSITIVE << std::endl;
        return FAIL;
    }

    // installs the signal handler
    sa.sa_handler = &signal_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        std::cerr << ERROR_MESSAGE_SIGACTION_ERROR << std::endl;
        delete scheduler;
        exit(EXIT_FAILURE);
    }

    // creates the scheduler
    scheduler = new ThreadsScheduler(quantum_usecs);

    create_main_thread();
    configure_timer(quantum_usecs);
    return SUCCESS;
}


int uthread_spawn(thread_entry_point entry_point) {
    int id;
    char *stack;
    address_t pc, sp;
    Thread *newThread;
    block_signals_set();

    // checks if entry_point is null
    if (entry_point == NULL){
        std::cerr << ERROR_MESSAGE_NULL_ENTRY_POINT << std::endl;
        return FAIL;
    }

    // gets the next available id
    id = scheduler->getNextAvailableId();
    if (id == FAIL) {
        std::cerr << ERROR_MESSAGE_NO_AVAILABLE_ID << std::endl;
        return FAIL;
    }

    // creates a new stack
    stack = new(std::nothrow) char[STACK_SIZE];
    if (stack == nullptr) {
        std::cerr << ERROR_MESSAGE_CANT_ALLOCATE_STACK << std::endl;
        return FAIL;
    }
    sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
    pc = (address_t) entry_point;

    // creates the new thread
    newThread = new Thread(id, stack, sp);

    // initializes the env of the new thread to use the right stack, and to run from the function 'entry_point',
    // when we'll use siglongjmp to jump into the thread.
    sigsetjmp(newThread->env, 1);
    ((newThread->env)->__jmpbuf)[JB_SP] = translate_address(sp);
    ((newThread->env)->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&(newThread->env)->__saved_mask);

    // updates the scheduler with the new thread
    scheduler->addNewThread(newThread, id);
    scheduler->addReadyThread(newThread);

    unblock_signals_set();
    return id;
}

int uthread_terminate(int tid) {
    Thread *currentThread;
    ThreadState state;
    block_signals_set();

    // checks if the tid is of the main and if the tid exists
    if (tid == 0) {
        delete scheduler;
        exit(EXIT_FAILURE);
    }
    if(scheduler->isTidExist(tid) == FAIL) {
        std::cerr << ERROR_MESSAGE_TID_NOT_EXISTS << std::endl;
        return FAIL;
    }

    // gets the current thread and terminates it according to it's state
    currentThread = scheduler->getThread(tid);
    state = currentThread->getState();
    if (state == RUNNING) {
        scheduler->setNextRunningThread(FALSE);
    }
    else if (state == READY) {
        scheduler->deleteReadyThread(currentThread);
    }
    else if (state == BLOCKED) {
        scheduler->deleteBlockedThread(currentThread);
    }

    scheduler->deleteThreadTid(tid);
    delete currentThread;
    unblock_signals_set();
    return SUCCESS;
}

int uthread_block(int tid) {
    Thread *currentThread;
    ThreadState state;
    block_signals_set();

    // checks if the tid is of the main and if the tid exists
    if(scheduler->isTidExist(tid) == FAIL || tid == 0) {
        std::cerr << ERROR_MESSAGE_TID_NOT_EXISTS << std::endl;
        return FAIL;
    }

    // gets the current thread and blocks it according to it's state
    currentThread = scheduler->getThread(tid);
    state = currentThread->getState();
    if (state == RUNNING) {
        scheduler->setNextRunningThread(FALSE);
    }
    else if (state == READY) {
        scheduler->deleteReadyThread(currentThread);
    }
    currentThread->setState(BLOCKED);
    scheduler->addBlockedThread(currentThread);

    unblock_signals_set();
    return SUCCESS;
}

int uthread_resume(int tid) {
    Thread *currentThread;
    block_signals_set();

    // checks if tid exists
    if (scheduler->isTidExist(tid) == FAIL) {
        std::cerr << ERROR_MESSAGE_TID_NOT_EXISTS << std::endl;
        return FAIL;
    }

    // gets the current thread, removes it from the blocked threads queue and adds it to the ready threads queue
    // after change its state
    currentThread = scheduler->getThread(tid);
    scheduler->deleteBlockedThread(currentThread);
    scheduler->addReadyThread(currentThread);
    currentThread->setState(READY);

    unblock_signals_set();
    return SUCCESS;
}

int uthread_sleep(int num_quantums){
    int tid;
    Thread *currentThread;
    tid = uthread_get_tid();
    currentThread = scheduler->getThread(tid);

    // checks if the running thread is the main thread
    if (tid == 0){
        std::cerr << ERROR_MESSAGE_MAIN_THREAD_CANT_SLEEP << std::endl;
        return FAIL;
    }

    // changes the running thread and adds sleep counter to the previous one
    scheduler->addSleepingThread(currentThread, num_quantums);
    scheduler->setNextRunningThread(TRUE);
    return SUCCESS;
}

int uthread_get_tid(){
    return scheduler->getRunningThreadTid();
}

int uthread_get_total_quantums(){
    return scheduler->getTotalQuantums();
}

int uthread_get_quantums(int tid){
    Thread *currentThread;
    int quantums;
    block_signals_set();

    // checks if the running thread is the main thread
    if (scheduler->isTidExist(tid) == FAIL){
        std::cerr << ERROR_MESSAGE_MAIN_THREAD_CANT_SLEEP << std::endl;
        return FAIL;
    }

    currentThread = scheduler->getThread(tid);
    quantums = currentThread->getThreadQuantums();
    unblock_signals_set();
    return quantums;
}



