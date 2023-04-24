#include <signal.h>
#include <setjmp.h>

#ifndef EX2_THREAD_H
#define EX2_THREAD_H

enum ThreadState{
    RUNNING,
    READY,
    BLOCKED,
    TERMINATED
};
typedef unsigned long address_t;

class Thread {
private:
    int _quantumCounter, _id;
    ThreadState _state;
    address_t _sp;
    char *_stack;
    // void (*_entry_point)(void);

public:
    sigjmp_buf env;

    Thread(int id, char *stack, address_t sp);
    ~Thread();

    int getId();
    void setState(ThreadState state);
    ThreadState getState();
    int getThreadQuantums();

};

#endif
