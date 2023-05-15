
#include "MapReduceFramework.h"
#include "Barrier.h"
#include <atomic>

typedef std::atomic<int> atomicIntCounter;
typedef std::atomic<uint64_t> atomicJobStage;

#define UNDEFINED 0
#define MAP 1
#define SHUFFLE 2
#define REDUCE 3

typedef struct {
    int threadID;
    pthread_t* thread;
//    std::atomic<uint64_t>* atomicState;
//    atomicIntCounter* atomicInputCounter;
//    atomicIntCounter* atomicReduceCounter;
    atomicIntCounter* intermediateCounter;
    atomicIntCounter* outputCounter;

    pthread_mutex_t* outputMutex;

    InputVec* inputVec;
    OutputVec* outputVec;
    IntermediateVec* intermediateVec;

} ThreadContext;

typedef struct {
    ThreadContext* threadContexts;
    JobState state;
    int multiThreadLevel;
    bool isJoined;

    InputVec* inputVec;
    OutputVec* outputVec;
    IntermediateVec* intermediateVec;

    pthread_mutex_t* outputMutex;
    Barrier barrier;
    MapReduceClient const *client;

    atomicIntCounter* intermediateCounter;
    atomicIntCounter* outputCounter;
    atomicIntCounter* inputCounter;

    atomicJobStage* atomicStage;

//    std::atomic<uint64_t>* atomicState;
//    atomicIntCounter *atomicInputCounter;
//    atomicIntCounter *atomicReduceCounter;
//    atomicIntCounter *atomicToShuffleCounter;

} JobContext;

int checkStage(JobContext* job){
    //TODO implement
}

void updateStage(JobContext* job, int stage){
    //TODO implement
    //     (*(context->atomicState)).fetch_add((uint64_t)1 << 62);
}

void mapPhase(JobContext* job){
    InputVec* inputVec = job->inputVec;
    if(checkStage(job) == UNDEFINED){
        updateStage(job, MAP);
    }
    int inputVecSize = inputVec->size();
    atomicIntCounter index = *(job->inputCounter)++;
    while (index < inputVecSize){
        auto pair = job->inputVec->at(index);
        job->client->map(pair.first, pair.second, job);
        //(*(job->atomicStage)).fetch_add(1<<31)
        index = (*(job->inputCounter))++;
    }



}

void emit2 (K2* key, V2* value, void* context){
    auto threadContext = (ThreadContext*) context;
    IntermediatePair pair = IntermediatePair(key, value);
    threadContext->intermediateVec->push_back(pair);
    (*(threadContext->intermediateCounter))++;
}

void emit3 (K3* key, V3* value, void* context){
    auto threadContext = (ThreadContext*) context;
    OutputPair pair = OutputPair(key, value);
    pthread_mutex_lock(threadContext->outputMutex);
    // TODO handle if error
    threadContext->outputVec->push_back(pair);
    pthread_mutex_unlock(threadContext->outputMutex);
    // TODO handle if error
    (*(threadContext->outputCounter))++;
}

JobContext* createJobContext(ThreadContext* threads, int multiThreadLevel){
    //TODO implement
}

ThreadContext* createThreadContext(atomicIntCounter* intermediateCounter, atomicIntCounter* outputCounter){
    //TODO implement
}

JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec,
                            int multiThreadLevel){
    ThreadContext* threads = new(std::nothrow) ThreadContext[multiThreadLevel];
    for(int i = 0; i < multiThreadLevel; i++){
        if (i == 0){
            int result = pthread_create(threads[i].thread, nullptr, mapShuffleReduce, nullptr);
            //TODO handle result- not 0 = error + change 4 arg
        }
        else{
            int result = pthread_create(threads[i].thread, nullptr, mapReduce, nullptr);
            //TODO handle result- not 0 = error + change 4 arg
        }
    }
    JobContext* jobContext = createJobContext(threads, multiThreadLevel);
    return jobContext;
}

void waitForJob(JobHandle job){
    auto jobContext = (JobContext*) job;
    if (!jobContext->isJoined){
        for(int i = 0; i < jobContext->multiThreadLevel; i++){
            int result = pthread_join(*jobContext->threadContexts[i].thread, nullptr);
            if (result != 0)
            {
                // TODO Error handling if pthread_join fails
            }
        }
    }
}
void getJobState(JobHandle job, JobState* state){
    auto jobContext = (JobContext*) job;
    //TODO
//    float percentage = (float) (((*(jobContext->atomicState)).load() >> 31) &
//                                (0x7fffffff)) / (float) jobContext->total;
//    int currStage = ((*(jobContext->atomicState)).load() >> 62) & 3;
    state->stage = jobContext->state.stage;
    state->percentage = jobContext->state.percentage;

}
void closeJobHandle(JobHandle job){
    waitForJob(job);
    auto jobContext = (JobContext*) job;
    for(int i = 0; i < jobContext->multiThreadLevel; i++){
        delete jobContext->threadContexts[i].thread;
    }
    delete jobContext->threadContexts;
    delete jobContext;
    //TODO free all the resources
}
