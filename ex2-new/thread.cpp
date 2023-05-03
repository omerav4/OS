#include "thread.h"
#include <iostream>

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
#define ERROR_MESSAGE_SIGEMPTYSET_ERROR "system error: sigemptyset failed\n"
#define MAIN_THREAD_ID 0

Thread::Thread(unsigned int id, char *stack, thread_entry_point entry_point)
{
    _id = id;
    _stack = stack;
    _quantum_counter = 0;
    _running_quantums = 0;   // TODO initialize with 1 or 0?
    _sleep_quantums = 0;
    _entry_point = entry_point;
    _state = READY;
    setup_thread();
}

/**
 * Default Ctor for the main thread
 */
Thread::Thread() {
    _id = MAIN_THREAD_ID;
    _entry_point = nullptr;
    _state = RUNNING;
    _stack = nullptr;
    _running_quantums = 1;   // TODO initialize with 1 or 0?
    _sleep_quantums = 0;
    setup_thread();
}

Thread::~Thread(){
    delete[] _stack;
}

unsigned Thread::get_id() const{
    return _id;
}

ThreadState Thread::get_state() const{
    return _state;
}

void Thread::set_state(ThreadState state){
    _state = state;
    if(state == RUNNING && _state != RUNNING){
        _quantum_counter += 1;
    }
}

int Thread::get_thread_quantums() const{
    return _quantum_counter;
}

int Thread::get_sleeping_quantums() const{
    return _sleep_quantums;
}

int Thread::get_running_quantums() const{
    return _running_quantums;
}

void Thread::increase_running_quantum() {
    printf("thread %d quantum %d", _id, _running_quantums);
    _running_quantums++;
}

void Thread::set_sleep_quantums(int num_quantums) {
    if(num_quantums < 0){
        return;
    }
    _sleep_quantums = num_quantums;
}

void Thread::setup_thread(){
    address_t sp = (address_t) _stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) _entry_point;
    sigsetjmp(env, 1);

    address_t trans_sp = translate_address(sp);
    _sp = trans_sp;
    address_t trans_pc = translate_address(pc);
    _pc = trans_pc;
    (env->__jmpbuf)[JB_SP] = trans_sp;
    (env->__jmpbuf)[JB_PC] = trans_pc;
    if (sigemptyset(&env->__saved_mask)== FAIL){
        std::cerr << ERROR_MESSAGE_SIGEMPTYSET_ERROR << std::endl;
        exit(EXIT_FAILURE);
    }
}


