#include "thread_scheduler.h"
#include <sys/time.h>

#define TO_SEC 1000000
#define FAIL -1
#define SUCCESS 0
#define MAIN_THREAD_ID 0
#define FROM_LONGJMP 1
#define FALSE -1
#define ERROR_MESSAGE_SETTIMER_ERROR "system error: settimer failed\n"
#define ERROR_MESSAGE_ALLOCATION_FAILURE "system error: allocation failure\n"


int ThreadsScheduler::quantum_counter = 1;


ThreadsScheduler::ThreadsScheduler(){
    ready_threads = new std::queue<Thread*>();
    if(ready_threads == nullptr){
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }

    blocked_threads = new std::queue<Thread*>();
    if(blocked_threads == nullptr){
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        delete ready_threads;
        exit(EXIT_FAILURE);
    }

    sleeping_threads = new std::queue<Thread*>();
    if(sleeping_threads == nullptr){
        delete ready_threads;
        delete blocked_threads;
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    running = nullptr;

    for (int i = 0; i < MAX_THREAD_NUM; i++){
        all_threads[i] = nullptr;
    }
}

ThreadsScheduler::~ThreadsScheduler(){
    free_resources();
}

void ThreadsScheduler::free_resources(){
    for (int i = 0; i < MAX_THREAD_NUM; i++){
        if(all_threads[i] != nullptr) {
            delete_thread_tid(i);
        }
    }
    delete ready_threads;
    delete blocked_threads;
    delete sleeping_threads;
}

int ThreadsScheduler::get_next_available_id(){
    //find the next minimum available tid
    for (int i = 1; i < MAX_THREAD_NUM; i++){
        if (all_threads[i] == nullptr){
            return i;
        }
    }
    return FAIL;
}

void ThreadsScheduler::add_new_thread(Thread *thread, unsigned int tid){
    all_threads[tid] = thread;
}

void ThreadsScheduler::set_running_thread(Thread *thread){
    thread->set_state(RUNNING);
    running = thread;
}

void ThreadsScheduler::add_ready_thread(Thread *thread){
    thread->set_state(READY);
    ready_threads->push(thread);
}

void ThreadsScheduler::add_blocked_thread(Thread *thread){
    thread->set_state(BLOCKED);
    blocked_threads->push(thread);
}

int ThreadsScheduler::is_tid_exist(unsigned int tid){
    if (tid >= MAX_THREAD_NUM || all_threads[tid] == nullptr || tid < 0){
        return FAIL;
    }
    return SUCCESS;
}

Thread* ThreadsScheduler::get_thread(unsigned int tid){
    return all_threads[tid];
}
void ThreadsScheduler::set_next_running_thread(){
    Thread* next_thread = ready_threads->front();    // get the next ready thread

    //if the ready threads queue is empty, we will continue with the current thread
    if (next_thread == nullptr){
        next_thread = running;
    }
    next_thread->set_state(RUNNING);
    running = next_thread;
    ready_threads->pop();
    next_thread->increase_running_quantum();
    quantum_counter++;   // update the total quantum counter after we change the running thread
    // siglongjmp(running->env, FROM_LONGJMP);

}

void ThreadsScheduler::delete_ready_thread(Thread *thread){
    // creates a helper queue, and iterates the ready threads queue until we will find the given thread, and we will
    // not add it to the helper queue
    std::queue<Thread*> *helper_queue;
    helper_queue = new std::queue<Thread*>();
    if(helper_queue == nullptr){
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        free_resources();
        exit(EXIT_FAILURE);
    }
    Thread *helper_thread;

    while(!ready_threads->empty()){
        helper_thread = ready_threads->front();
        if(helper_thread != thread){
            helper_queue->push(helper_thread);
        }
        ready_threads->pop();
    }
    ready_threads = helper_queue;
}

void ThreadsScheduler::delete_blocked_thread(Thread *thread){
    // creates a helper queue, and iterates the blocked threads queue until we will find the given thread, and we will
    // not add it to the helper queue
    std::queue<Thread*> *helper_queue;
    helper_queue = new std::queue<Thread*>();
    if(helper_queue == nullptr){
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        free_resources();
        exit(EXIT_FAILURE);
    }
    Thread *helper_thread;

    while(!blocked_threads->empty()){
        helper_thread = blocked_threads->front();
        if(helper_thread != thread){
            helper_queue->push(helper_thread);
        }
        blocked_threads->pop();
    }
    ready_threads = helper_queue;
}

void ThreadsScheduler::delete_thread_tid(unsigned int tid) {
    // change the pointer in the all_threads array of the given tid to null and free the resources of that thread
    delete all_threads[tid];
    all_threads[tid] = nullptr;
}

sigset_t* ThreadsScheduler::get_signals_set(){
    return &signals_set;
}

unsigned int ThreadsScheduler::get_running_thread_tid(){
    return running->get_id();
}

Thread* ThreadsScheduler::get_running_thread(){
    return running;
}

int ThreadsScheduler::get_total_quantums(){
    return quantum_counter;
}

void ThreadsScheduler::add_sleeping_thread(Thread *thread, int num_quantums){
    thread->set_sleep_quantums(num_quantums);
    sleeping_threads->push(thread);
}

void ThreadsScheduler::update_sleeping_threads(){
    // creates a helper queue, and iterates the sleeping threads queue until we will find the given thread, update their
    // sleeping counter, and if there is an unblocked tread with sleeping counter that equals to 0, adds it to the ready
    // threads queue
    std::queue<Thread*> *helper_queue;
    helper_queue = new std::queue<Thread*>();
    if(helper_queue == nullptr){
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        free_resources();
        exit(EXIT_FAILURE);
    }

    Thread *sleeping_thread;
    while(!sleeping_threads->empty()){
        sleeping_thread = sleeping_threads->front();
        sleeping_thread->set_sleep_quantums(sleeping_thread->get_sleeping_quantums()-1);
        if ((sleeping_thread->get_sleeping_quantums() == 0) && sleeping_thread->get_state() != BLOCKED){
            add_ready_thread(sleeping_thread);
        }
        else{
            helper_queue->push(sleeping_thread);
        }
        sleeping_threads->pop();
    }
    sleeping_threads = helper_queue;
}
