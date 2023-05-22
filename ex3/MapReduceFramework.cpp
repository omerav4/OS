#include "MapReduceFramework.h"
#include "Resources (1)/Barrier/Barrier.h"
#include <atomic>
#include <iostream>


///--------------------------------- macros -----------------------------------
#define STAGE_MASK 0x3
#define PROCESSED_KEYS_MASK(jobState) jobState&0x7fffffff
#define JOB_STAGE(jobState) jobState & 0x3
#define TOTAL_KEYS(jobState) jobState & 0x7fffffff
#define INCREMENT_ALREADY_PROCESSED(jobState) ((jobState).fetch_add(1 << 2))
#define ERROR_MESSAGE_ALLOCATION_FAILURE "system error: allocation failure\n"

///-------------------------------- typedefs ----------------------------------
typedef std::atomic<int> atomicIntCounter;

// two least significant bits are for STAGE enum representation
// next 31 bits are for already processed keys
// last 31 bits are for
typedef std::atomic<uint64_t> atomicJobStage;
typedef std::vector<IntermediateVec*> shuffeledVector;
typedef struct JobContext JobContext;
typedef struct ThreadContext ThreadContext;

// TODO describe
struct ThreadContext{
    JobContext* job;
    pthread_t* thread;
    IntermediateVec* intermediateVec;
};

// TODO describe
struct JobContext{
    ThreadContext* threadContexts;
    JobState state;
    int multiThreadLevel;
    bool isJoined;

    InputVec* inputVec;
    OutputVec* outputVec;

    pthread_mutex_t* mutex;
    Barrier* barrier;
    MapReduceClient const *client;

    atomicIntCounter* intermediateCounter;
    atomicIntCounter* outputCounter;
    atomicIntCounter* inputCounter;

    atomicJobStage* atomicStage;
    shuffeledVector* shuffeledVector;
};

///-------------------------------- free resources ------------------------------------

void freeThreadContexts(ThreadContext* threads, int len) {
    for(int i = 0; i < len; i++) {
        delete (threads + i) ->intermediateVec;
    }
}

void freeJobContext(JobContext* job) {
    freeThreadContexts(job->threadContexts, job->multiThreadLevel);
    delete job->shuffeledVector;
    delete job->barrier;
    delete job->atomicStage;
    if (pthread_mutex_destroy(job->mutex) != 0) {
        fprintf(stderr, "[[Barrier]] error on pthread_mutex_destroy");
        exit(EXIT_FAILURE);
    }
}

///-------------------------------- create structs ------------------------------------
// TODO describe
ThreadContext* createThreadContext(JobContext* jobContext)
{
    ThreadContext* threadContext = new (std::nothrow) ThreadContext;
    if (!threadContext) {
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    threadContext->thread = new (std::nothrow) pthread_t;
    threadContext->intermediateVec = new (std::nothrow) IntermediateVec;
    threadContext->job = jobContext;

    if (!threadContext->thread || !threadContext->intermediateVec) {
        delete threadContext->thread;
        delete threadContext->intermediateVec;
        delete threadContext;
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    return threadContext;
}

// TODO describe
JobContext* createJobContext(ThreadContext* threads, int multiThreadLevel,
                             InputVec* inputVec, OutputVec* outputVec,
                             const MapReduceClient& client)
{
    JobContext* jobContext = new (std::nothrow) JobContext;
    if (!jobContext) {
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    jobContext->multiThreadLevel = multiThreadLevel;
    jobContext->threadContexts = threads;
    jobContext->client = &client;
    jobContext->inputVec = inputVec;
    jobContext->outputVec = outputVec;

    jobContext->shuffeledVector = new (std::nothrow) shuffeledVector;
    jobContext->atomicStage = new (std::nothrow) atomicJobStage;
    jobContext->barrier = new Barrier(multiThreadLevel);
    jobContext->mutex = pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER);

    // TODO need the counters?
    jobContext->outputCounter = new (std::nothrow) atomicIntCounter;
    jobContext->inputCounter = new (std::nothrow) atomicIntCounter;

    // Check for allocation failures
    if (!jobContext->mutex || !jobContext->barrier || !jobContext->outputCounter || !jobContext->inputCounter ||
        !jobContext->atomicStage || !jobContext->shuffeledVector) {
        freeThreadContexts(threads, multiThreadLevel);
        delete jobContext->shuffeledVector;
        delete jobContext->atomicStage;
        delete jobContext->inputCounter;
        delete jobContext->outputCounter;
        delete jobContext->barrier;
        delete jobContext->mutex;
        delete jobContext;
        }
    }

///-------------------------------- phases ------------------------------------
int checkStage(JobContext* job)
{
    auto state = (*(job->atomicStage)).load();
    return STAGE_MASK(state);
}

void updateStage(JobContext* job, int stage){
    int atomicStage = (*(job->atomicStage)).load();
    atomicStage &= ~(JOB_STAGE(atomicStage));  // Clear the existing stage bits
    atomicStage |= JOB_STAGE(stage);  // Set the new stage bits
    (*(job->atomicStage)).store(atomicStage); // Save the new stage
}

void mapPhase(ThreadContext* thread, JobContext* job)
{
    InputVec *inputVec = job->inputVec;
    if (checkStage(job) == UNDEFINED_STAGE)
    {
        updateStage(job, MAP_STAGE);
    }
    int inputVecSize = inputVec->size();
    atomicIntCounter index = (*(job->inputCounter))++;        // TODO check if it's supposed to be thread
    while (index < inputVecSize)
    {
        auto pair = inputVec->at(index);
        job->client->map(pair.first, pair.second, job);
        (*(job->atomicStage)).fetch_add(1 << 31);
        index = (*(job->inputCounter))++;
    }
}

bool compare(IntermediatePair pair1, IntermediatePair pair2){
    return pair1.first < pair2.first
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
    bool empty = true;
    while(true)
    {
        // find the minimal key
        for (int i = 0; i < multiThreadLevel; i++)
        {
            ThreadContext* currentThread = job->threadContexts + i;
            K2* currentKey = currentThread->intermediateVec->at(0).first;
            if(currentKey == nullptr){
                continue;
            }
            else if(currentKey < minKey){
                minKey = currentKey;
            }
            empty = false;
        }
        if(empty){
            break;
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
            INCREMENT_ALREADY_PROCESSED(*(job->atomicStage));  // TODO check where it's supposed to be

        }
    job->shuffeledVector->push_back(temp);
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

void* mapReduce(ThreadContext* thread){
    ThreadContext* threadContext = (ThreadContext*) thread;
    JobContext* job = threadContext->job;
    mapPhase(thread, job);
    sortPhase(thread);
    waitForAllThreads(job);  // verify all threads finished map & sort
    waitForAllThreads(job);  // verify thread 0 finished shuffle
    reducePhase(job);
}

void* mapShuffleReduce(ThreadContext* thread, JobContext* job){
    mapPhase(thread, job);          // TODO make sure that mapPhase gets the job as a parameter in pthread_init
    sortPhase(thread);
    waitForAllThreads(job);
    // set to zero percentage counter
    (*(job->atomicStage)) = *(job->atomicStage) & (3 << 62);    // TODO set to zero percentage counter?!
    // TODO shuffle counter??????
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
    pthread_mutex_lock(threadContext->mutex);
    // TODO handle if error
    threadContext->outputVec->push_back(pair);
    pthread_mutex_unlock(threadContext->mutex);
    // TODO handle if error
    (*(threadContext->outputCounter))++;
}

JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec,
                            int multiThreadLevel){
    ThreadContext* threads = new(std::nothrow) ThreadContext[multiThreadLevel];

    JobContext* jobContext = createJobContext(threads, multiThreadLevel,
                                              inputVec, outputVec, client);
    for(int i = 0; i < multiThreadLevel; i++){
        if (i == 0){
            int result = pthread_create(threads[i].thread, nullptr, mapShuffleReduce,
                                        threads[0]);
            //TODO handle result- not 0 = error
        }
        else{
            int result = pthread_create(threads[i].thread, nullptr, mapReduce,
                                        threads[i]);
            //TODO handle result- not 0 = error
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
    auto jobContext = ((JobContext*) job);
    auto state = (*(jobContext->atomicStage)).load()
    stage_t stage = JOB_STAGE(state);
    float percentage = (float)PROCESSED_KEYS_MASK(state) /
            (float)TOTAL_KEYS(state);
    state->stage = stage;
    state->percentage = percentage;
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

void freeResources(jobContext* job)
{
    //TODO free all the resources
}
