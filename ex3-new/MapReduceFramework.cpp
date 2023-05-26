#include "MapReduceFramework.h"
#include "Barrier.h"
#include <atomic>
#include <iostream>
#include <algorithm>
#include <pthread.h>


///--------------------------------- macros -----------------------------------
#define PROCESSED_KEYS_MASK(jobState) jobState&0x7fffffff
#define JOB_STAGE(jobState) jobState & 0x3
#define TOTAL_KEYS(jobState) jobState & 0x7fffffff
#define INCREMENT_BY_ONE(jobState) ((jobState).fetch_add(1 << 2))
#define INCREMENT_BY_VALUE(jobState, value) ((jobState).fetch_add(value << 2))

#define ERROR_MESSAGE_ALLOCATION_FAILURE "system error: allocation failure\n"
#define ERROR_MESSAGE_PTHREAD_FAILURE "system error: pthread creation failed\n"
#define JOIN_FAILURE "system error: pthread join  failed :(\n"
#define MUTEX_UNLOCK_FAILURE "system error: mutex unlock  failed\n"
#define MUTEX_LOCK_FAILURE "system error: mutex lock  failed\n"
#define MUTEX_DESTROY_ERR "system error: [[Barrier]] error on pthread_mutex_destroy"
///-------------------------------- typedefs ----------------------------------
typedef std::atomic<int> atomicIntCounter;

// two least significant bits are for STAGE enum representation
// next 31 bits are for already processed keys
// last 31 bits are for
typedef std::atomic<uint64_t> atomicJobStage;   // TODO - maybe delete
typedef std::vector<IntermediateVec> ShuffledVector;
typedef struct JobContext JobContext;
typedef struct ThreadContext ThreadContext;

/**
 *
 */
struct ThreadContext{
    int id;
    JobContext* job;
    pthread_t* thread;
    IntermediateVec* intermediateVec;
};

/**
 *
 */
struct JobContext{
    // given by user
    MapReduceClient const *client;
    Barrier* barrier;
    int multiThreadLevel;
    const InputVec* inputVec;
    OutputVec* outputVec;

    // independent fields
    ThreadContext* threadContexts;
    int processedKeys;   /// TODO - should be atomic?
    JobState state;   /// TODO should we change to an atomic var?
    atomicIntCounter* indexCounter;  //
    std::atomic<int> nextPhaseInputSize; //
    pthread_mutex_t mutex; //
    pthread_mutex_t emitMutex;  /// TODO - is possible with one mutex?
    bool isJoined;  //
    ShuffledVector shuffledVec; // vector of vectors for shuffle phase
    ShuffledVector vecToReduce; // vector of vectors for reduce phase
};

///-------------------------------- free resources ------------------------------------
/**
 *
 * @param threads
 * @param len
 */
void freeThreadContexts(ThreadContext* threads, int len) {
    for(int i = 0; i < len; i++) {
        delete (threads + i) ->intermediateVec;
        delete (threads + i)->thread;
        /// TODO do we need to delete pthread?
    }
}
/**
 *
 * @param job
 */
void freeJobContext(JobContext* job) {
    freeThreadContexts(job->threadContexts, job->multiThreadLevel);
    delete job->indexCounter;
    delete job->barrier;
    if (pthread_mutex_destroy(&job->mutex) != 0) {
        std::cerr << MUTEX_DESTROY_ERR;
        exit(EXIT_FAILURE);
    }
}

///-------------------------------- create structs ------------------------------------
/**
 *
 * @param jobContext
 * @param id
 * @return
 */
ThreadContext* createThreadContext(JobContext* jobContext, int id)
{
    auto* threadContext = new (std::nothrow) ThreadContext;
    if (!threadContext)
    {    // allocation failed
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    threadContext->thread = new (std::nothrow) pthread_t;
    threadContext->intermediateVec = new (std::nothrow) IntermediateVec;
    threadContext->job = jobContext;
    threadContext->id = id;

    if (!threadContext->thread || !threadContext->intermediateVec)
    {   // allocation failed
        delete threadContext->thread;
        delete threadContext->intermediateVec;
        delete threadContext;
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    return threadContext;
}

/**
 *
 * @param threads
 * @param multiThreadLevel
 * @param inputVec
 * @param outputVec
 * @param client
 * @return
 */
JobContext* createJobContext(ThreadContext* threads, int multiThreadLevel, const InputVec& inputVec,
                             OutputVec& outputVec, const MapReduceClient& client)
{
    JobContext* jobContext = new (std::nothrow) JobContext;
    if (!jobContext) {
        std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
        exit(EXIT_FAILURE);
    }
    jobContext->multiThreadLevel = multiThreadLevel;
    jobContext->threadContexts = threads;
    jobContext->client = &client;
    jobContext->inputVec = &inputVec;
    jobContext->outputVec = &outputVec;
    jobContext->processedKeys = 0;

    jobContext->shuffledVec =  ShuffledVector(multiThreadLevel);  //TODO - is this good?
    jobContext->barrier = new Barrier(multiThreadLevel);;
    jobContext->indexCounter = new (std::nothrow) atomicIntCounter;

    jobContext->mutex = pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER);
    jobContext->emitMutex = pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER);

    jobContext->shuffledVec = std::vector<IntermediateVec>(multiThreadLevel);  //TODO change names, add checking of allocation
    jobContext->vecToReduce = std::vector<IntermediateVec>();
    jobContext->nextPhaseInputSize = 0;

    // Check for allocation failures
    if ( !jobContext->barrier || !jobContext->indexCounter) {
        freeJobContext(jobContext);
        }
    return jobContext;
}

///-------------------------- check and update stage --------------------------



///-------------------------------- phases ------------------------------------
/**
 *
 * @param thread
 * @param job
 */
void mapPhase(ThreadContext* thread, JobContext* job)
{
    if (job->state.stage == UNDEFINED_STAGE)
    {
        auto stage = MAP_STAGE;
        float percentage = 0;
        job->state = {stage, percentage};
    }
    unsigned long inputVecSize = job->inputVec->size();
    uint index = (*(job->indexCounter))++;
    while (index < inputVecSize)
    {
        auto pair = job->inputVec->at(index);
        job->client->map(pair.first, pair.second, thread);
        //INCREMENT_BY_ONE(*(job->atomicStage));
        pthread_mutex_lock(&job->mutex); // TODO handle if error
        job->processedKeys += 1;
        job->state.percentage =  100 * ((float) job->processedKeys /(float) inputVecSize);
        pthread_mutex_unlock(&job->mutex); // TODO handle if error
        index = (*(job->indexCounter))++;
    }
}
/**
 *
 * @param pair1
 * @param pair2
 * @return
 */
bool compare(IntermediatePair pair1, IntermediatePair pair2){
    return pair1.first->operator<(*pair2.first);
}

void shufflePhase(JobContext* job){
    IntermediateVec allThreadVec;
    for(const IntermediateVec& vec: (job->shuffledVec))
    {
        allThreadVec.insert(allThreadVec.end(), vec.begin(), vec.end()); // combine  threads' vectors
    }
    if(!allThreadVec.empty())
    {
        std::sort(allThreadVec.begin(), allThreadVec.end(), compare);

        IntermediatePair prev_pair = {nullptr, nullptr};
        IntermediatePair curr_pair;

        while(!allThreadVec.empty()){
            curr_pair = allThreadVec.back();
            if (prev_pair.first == nullptr || (prev_pair.first != nullptr && (compare(curr_pair, prev_pair)
            || compare(prev_pair, curr_pair)))) // we are in the first pair or a pair with new key
            {
                IntermediateVec new_vec = IntermediateVec();  // creat vec for cur key
                job->vecToReduce.insert(job->vecToReduce.begin(), new_vec);  // insert to reduce vec
            }
            job->vecToReduce[0].push_back(curr_pair);  // cur pair has the same key of recently created vec
            prev_pair = curr_pair;  // update
            allThreadVec.pop_back();  // pop all

            job->processedKeys++;
            job->state.percentage = 100 * (float)(job->processedKeys) /  (float)(job->nextPhaseInputSize);
    }
//    K2* maxKey;
//
//    int multiThreadLevel = job->multiThreadLevel;
//    // checks if all intermediate vectors are empty
//    for (int i = 0; i < multiThreadLevel; i++){
//        maxKey = (job->threadContexts+i)->intermediateVec->back().first;
//        if (maxKey != nullptr) {
//            break;
//        }
//    }
//    if (maxKey == nullptr){
//        return;
//    }
//
//    bool empty = true;
//    while(true)
//    {
//        // checks for the maximum key
//        for (int i = 0; i < multiThreadLevel; i++)
//        {
//            ThreadContext* currentThread = job->threadContexts + i;
//            K2* currentKey = currentThread->intermediateVec->back().first;
//            if(currentKey == nullptr){
//                continue;
//            }
//            else if(currentKey > maxKey){
//                maxKey = currentKey;
//            }
//            empty = false;
//        }
//        if(empty){
//            break;
//        }
//
//        // create a vector of the values of the maximal key, adds it to the temp vector, and adds the temp vector
//        // to the shuffledVec of the job
//        auto temp = new IntermediateVec();
//        for(int i = 0; i < multiThreadLevel; i++) {
//            ThreadContext *currentThread = job->threadContexts + i;
//            IntermediateVec *currentVec = currentThread->intermediateVec;
//            K2 *currentKey = currentVec->back().first;
//            if (currentKey == maxKey) {
//                IntermediatePair pair = currentVec->back();
//                currentVec->erase(currentVec->end());
//                temp->push_back(pair);
//                //INCREMENT_BY_ONE(*(job->atomicStage));
//            }
//        }
//        job->shuffledVec->push_back(temp);
//        job->processedKeys++;
//        job->state.percentage = 100 * (float) job->processedKeys / (float) job->inputVec->size();
//        job->processedKeys = 0;
//
    }
}

void reducePhase(ThreadContext* threadContext){
//    if(checkStage(job) == SHUFFLE_STAGE){
//        job->state.stage = REDUCE_STAGE;
//        job->state.percentage = 0;
//        updateStage(job, REDUCE_STAGE);
//    }
    JobContext* job = threadContext->job;
    unsigned long vecToReduceSize = job->vecToReduce.size();
    unsigned long inputVecSize = job->inputVec->size();
    uint index = (*(job->indexCounter))++;
    while (index < vecToReduceSize){
        auto currentVector = (job->vecToReduce)[index];
        job->client->reduce(&currentVector, threadContext);
//        INCREMENT_BY_VALUE(*(job->atomicStage), currentVector->size());

        pthread_mutex_lock(&job->mutex); // TODO handle if error
        job->processedKeys += (int)currentVector.size();
        job->state.percentage =  100 * ((float) job->processedKeys /(float) job->nextPhaseInputSize);
        pthread_mutex_unlock(&job->mutex); // TODO handle if error
        index = (*(job->indexCounter))++;
    }
}

///------------------------------------------- flows -------------------------------------------------------
void waitForAllThreads(JobContext* job){
    job->barrier->barrier();
}

void* mapReduce(void* context){
    auto thread = (ThreadContext*) context;
    JobContext* job = thread->job;
    mapPhase(thread, job);
//    sortPhase(thread);
    std::sort((job->shuffledVec)[thread->id].begin(), job->shuffledVec[thread->id].end(), compare);
    waitForAllThreads(job);  // verify all threads finished map & sort

    if(thread->id == 0){
        pthread_mutex_lock(&(job->mutex)); // TODO handle error
        job->processedKeys = 0;
        job->state.percentage = 0;
        job->state.stage = SHUFFLE_STAGE;

        pthread_mutex_unlock(&(job->mutex));    // TODO handle error

        shufflePhase(job);;

        pthread_mutex_lock(&(job->mutex));
        job->processedKeys = 0;
        job->state.percentage = 0;
        job->state.stage = REDUCE_STAGE;
        job->indexCounter->store(0);
        pthread_mutex_unlock(&(job->mutex));    // TODO handle error
    }

    waitForAllThreads(job);  // verify thread 0 finished shuffle
    reducePhase(thread);
    return nullptr;
}


///------------------------------------------- library -------------------------------------------------------

void emit2 (K2* key, V2* value, void* context)
{
    auto threadContext = (ThreadContext*) context;
    auto job = threadContext->job;
    int result = pthread_mutex_lock(&job->emitMutex);
    if(result!=0){
        std::cerr <<MUTEX_LOCK_FAILURE;
        exit(EXIT_FAILURE);
    }
    IntermediatePair pair = IntermediatePair(key, value);
    (job->shuffledVec)[threadContext->id].push_back(pair);
    result = pthread_mutex_unlock(&job->emitMutex);
    if(result!=0){
        std::cerr <<MUTEX_UNLOCK_FAILURE;
        exit(EXIT_FAILURE);
    }
    job->nextPhaseInputSize++;
    //INCREMENT_BY_ONE(*(threadContext->job->atomicStage));

}

void emit3 (K3* key, V3* value, void* context){
    auto threadContext = (ThreadContext*) context;
    auto job = threadContext->job;
    int result = pthread_mutex_lock(&job->emitMutex);
    if(result!=0){
        std::cerr <<MUTEX_LOCK_FAILURE;
        exit(EXIT_FAILURE);
    }
    OutputPair pair = OutputPair(key, value);
    job->outputVec->push_back(pair);
    result = pthread_mutex_unlock(&job->emitMutex);
    if(result!=0){
        std::cerr <<MUTEX_UNLOCK_FAILURE;
        exit(EXIT_FAILURE);
    }
//    INCREMENT_BY_VALUE(*(job->atomicStage), currentVector->size());
}

JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec, int multiThreadLevel)
{
    auto* threads = new(std::nothrow) ThreadContext[multiThreadLevel];

    JobContext* jobContext = createJobContext(threads, multiThreadLevel, inputVec, outputVec, client);
    for(int i = 0; i < multiThreadLevel; i++){
        threads[i] = *createThreadContext(jobContext, i);
        int result = pthread_create((threads + i)->thread, nullptr,mapReduce, threads +i);
        if(result !=0){
            std::cerr <<ERROR_MESSAGE_PTHREAD_FAILURE;
            exit(EXIT_FAILURE);
        }
    }
    return jobContext;
}

void waitForJob(JobHandle job){
    auto jobContext = static_cast<JobContext*>(job);
    if (!jobContext->isJoined){
        jobContext->isJoined = true;
        for(int i = 0; i < jobContext->multiThreadLevel; i++){
            int result = pthread_join(*jobContext->threadContexts[i].thread, nullptr);
            if (result != 0)
            {
                std::cerr <<JOIN_FAILURE;
                exit(EXIT_FAILURE);
            }
        }
    }
}

void getJobState(JobHandle job, JobState* state){
    auto jobContext = static_cast<JobContext*>(job);
    state->stage = jobContext->state.stage;
    state->percentage = jobContext->state.percentage;
}
void closeJobHandle(JobHandle job){
    waitForJob(job);
    auto jobContext = static_cast<JobContext*>(job);
    freeJobContext(jobContext);
}

