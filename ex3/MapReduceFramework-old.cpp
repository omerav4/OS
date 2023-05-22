
#include "MapReduceFramework.h"
#include "Resources (1)/Barrier/Barrier.h"
#include <atomic>

///------------------------------------------- typedefs -------------------------------------------------------
typedef std::atomic<int> atomicIntCounter;
typedef std::atomic<uint64_t> atomicJobStage;
typedef std::vector<IntermediateVec*> shuffeledVector;

typedef struct {
    int threadID;
    pthread_t* thread;
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
    Barrier* barrier;
    MapReduceClient const *client;

    atomicIntCounter* intermediateCounter;
    atomicIntCounter* outputCounter;
    atomicIntCounter* inputCounter;

    atomicJobStage* atomicStage;
    shuffeledVector* shuffeledVector;
} JobContext;

///------------------------------------------- phases -------------------------------------------------------
int checkStage(JobContext* job){
    //TODO implement
}

void updateStage(JobContext* job, int stage){
    //TODO implement
    //     (*(context->atomicState)).fetch_add((uint64_t)1 << 62);
}

void mapPhase(ThreadContext* thread, JobContext* job){
    InputVec* inputVec = thread->inputVec;
    if(checkStage(job) == UNDEFINED_STAGE){
        updateStage(job, MAP_STAGE);
    }
    int inputVecSize = inputVec->size();
    atomicIntCounter index = (*(job->inputCounter))++;              // TODO check if it's supposed to be thread
    while (index < inputVecSize){
        auto pair = inputVec->at(index);
        job->client->map(pair.first, pair.second, job);
        (*(job->atomicStage)).fetch_add(1<<31);
        index = (*(job->inputCounter))++;
    }
}

bool compare(IntermediatePair pair1, IntermediatePair pair2){
    // TODO implement
}

void sortPhase(ThreadContext* thread){
    std::sort(thread->intermediateVec->begin(),thread->intermediateVec->end(), compare);
}

void shufflePhase(ThreadContext* thread, JobContext* job){
    K2* minKey;
    if(checkStage(job) == MAP_STAGE){
        updateStage(job, SHUFFLE_STAGE);
    }

    // checks if all intermediate vectors are empty
    for (int i = 0; i < job->multiThreadLevel; i++){
        if (minKey == nullptr){
            minKey = thread->intermediateVec->at(0).first;
        }
        else{
            break;
        }
    }
    if (minKey == nullptr){
        return;
    }
    int multiThreadLevel = job->multiThreadLevel;
    while(true){
        // find the minimal key
        for (int i = 0; i < multiThreadLevel; i++){
            ThreadContext* currentThread = job->threadContexts + i;
            K2* currentKey = currentThread->intermediateVec->at(0).first;
            if(currentKey == nullptr){
                continue;
            }
            else if(currentKey < minKey){
                minKey = currentKey;
            }
        }
        IntermediateVec* temp = new IntermediateVec();
        for(int i = 0; i < multiThreadLevel; i++){
            ThreadContext* currentThread = job->threadContexts + i;
            K2* currentKey = currentThread->intermediateVec->at(0).first;
            if(currentKey == minKey){
                IntermediateVec* currentVector = currentThread->intermediateVec;
                IntermediatePair pair = currentVector->at(0);
                currentVector->erase(currentVector->begin());
                temp->push_back(pair);
                //(*(job->atomicStage)).fetch_add(1 << 31);       // TODO check where it's supposed to be

            }
        job->shuffeledVector->push_back(temp);
        }
    }
}

void reducePhase(JobContext* job){
    shuffeledVector* shuffeledVector = job->shuffeledVector;
    if(checkStage(job) == SHUFFLE_STAGE || checkStage(job) == MAP_STAGE){
        updateStage(job, REDUCE_STAGE);
    }
    int shuffeledVecSize = shuffeledVector->size();
    atomicIntCounter index = (*(job->outputCounter))++;              // TODO check if it's supposed to be thread
    while (index < shuffeledVecSize){
        auto currentVector = shuffeledVector->at(index);
        job->client->reduce(currentVector, job);
        (*(job->atomicStage)).fetch_add(currentVector->size() <<31);
        index = (*(job->outputCounter))++;
    }
}

///------------------------------------------- flows -------------------------------------------------------
void waitForAllThreads(JobContext* job){
    job->barrier->barrier();
}

void* mapReduce(ThreadContext* thread, JobContext* job){
    mapPhase(thread, job);          // TODO make sure that mapPhase gets the job as a parameter in pthread_init
    sortPhase(thread);
    waitForAllThreads(job);
    reducePhase(job);
}

void* mapShuffleReduce(JobContext* job){
    mapPhase(thread, job);          // TODO make sure that mapPhase gets the job as a parameter in pthread_init
    sortPhase(thread);
    waitForAllThreads(job);
    // set to zero percentage counter
    (*(job->atomicStage)) = *(job->atomicStage) & (3 << 62);    // TODO set to zero percentage counter?!
    // TODO shuffle counter?
    shufflePhase(thread, job);
    waitForAllThreads(job);         // TODO needed?
    reducePhase(job);
}

///------------------------------------------- library -------------------------------------------------------

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
    JobContext* jobContext = createJobContext(threads, multiThreadLevel);
    for(int i = 0; i < multiThreadLevel; i++){
        if (i == 0){
            int result = pthread_create(threads[i].thread, nullptr, mapShuffleReduce, jobContext);
            //TODO handle result- not 0 = error + change 4 arg
        }
        else{
            int result = pthread_create(threads[i].thread, nullptr, mapReduce, jobContext);
            //TODO handle result- not 0 = error + change 4 arg
        }
    }
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
