
#include "thread.h"

Thread::Thread(int id, char *stack, address_t sp)
{
    _id = id;
    _stack = stack;
    _sp = sp;
    _quantumCounter = 0;
    _state = READY;
}

Thread::~Thread(){
    delete[] _stack;
}

int Thread::getId(){
    return _id;
}

ThreadState Thread::getState(){
    return _state;
}

void Thread::setState(ThreadState state){
    _state = state;
    if(state == RUNNING && _state != RUNNING){
        _quantumCounter += 1;
    }
}

int Thread::getThreadQuantums(){
    return _quantumCounter;
}
