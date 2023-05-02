#include <signal.h>
#include <setjmp.h>

#ifndef EX2_THREAD_H
#define EX2_THREAD_H
#define FAIL -1

#define SECOND 1000000
#define STACK_SIZE 4096

enum ThreadState{
    RUNNING,
    READY,
    BLOCKED,
};
typedef unsigned long address_t;
typedef void (*thread_entry_point)(void);

class Thread {
private:
    unsigned int _id;
    int _quantumCounter;
    ThreadState _state;
    address_t _sp;
    address_t _pc;
    char* _stack;
    thread_entry_point _entry_point;
    int _running_quantums;
    unsigned int _sleep_quantums;

public:
    sigjmp_buf env;

    Thread(unsigned int id, char *stack, thread_entry_point entry_point);
    Thread();
    ~Thread();

    unsigned int getId() const;
    void setState(ThreadState state);
    ThreadState getState() const;
    int getThreadQuantums() const;
    int getSleepingQuantums() const;
    int getRunningQuantums() const;
    void increase_running_quantum();
    void set_sleep_quantums(int num_quantums);

private:
    void setup_thread();

};

#endif
