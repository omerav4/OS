#include "MapReduceFramework.h"
#include "Barrier.h"
#include <atomic>
#include <iostream>
#include <algorithm>
#include <pthread.h>
#include <bitset>


///--------------------------------- macros -----------------------------------
#define PROCESSED_KEYS_MASK(jobState) jobState&0x7fffffff
#define JOB_STAGE(jobState) jobState & 0x3
#define TOTAL_KEYS(jobState) jobState & 0x7fffffff
#define INCREMENT_BY_ONE(jobState) ((jobState).fetch_add(1 << 2))
#define INCREMENT_BY_VALUE(jobState, value) ((jobState).fetch_add(value << 2))

#define ERROR_MESSAGE_ALLOCATION_FAILURE "system error: allocation failure\n"
#define ERROR_MESSAGE_PTHREAD_FAILURE "system error: pthread creation failed\n"
#define JOIN_FAILURE "system error: pthread join  failed\n"
#define MUTEX_UNLOCK_FAILURE "system error: mutex unlock  failed\n"
#define MUTEX_LOCK_FAILURE "system error: mutex lock  failed\n"
#define MUTEX_DESTROY_ERR "system error: [[Barrier]] error on pthread_mutex_destroy"

///-------------------------------- typedefs ----------------------------------
typedef std::atomic<uint64_t> atomicIntCounter;

// two least significant bits are for STAGE enum representation
// next 31 bits are for already processed keys
// last 31 bits are for total keys
typedef std::atomic<uint64_t> atomicJobStage;
typedef std::atomic<bool> atomicBoolFlag;

typedef std::vector<IntermediateVec> IntermediateVecsVector;
typedef struct JobContext JobContext;
typedef struct ThreadContext ThreadContext;

/**
 * ThreadContext struct which represents the context of each thread.
 * Includes the thread's id, job, the thread itself and the intermediate vector.
 */
struct ThreadContext{
    int id;
    JobContext* job;
    pthread_t* thread;
    IntermediateVec* intermediateVec;
};

/**
 * JobContext struct which represents the context of each job.
 * Includes the job's client, barrier, input vector, output vector, multi thread level, array of all the thread contexts,
 * an atomic variable for the job's state and processed keys counter, mutex, index counter for each phase, shuffeled
 * vector for the reduce phase, etc.
 */
struct JobContext{
    // given by user
    MapReduceClient const *client;
    Barrier* barrier;
    int multiThreadLevel;
    const InputVec* inputVec;
    OutputVec* outputVec;

    // independent fields
    ThreadContext* threadContexts;  // array of the threads contexts
    atomicBoolFlag isJoined;  // indicates that a thread called the pthread_join function
    IntermediateVecsVector allIntermediateVecs; // vector of vectors for shuffle phase
    IntermediateVecsVector vecToReduce; // vector of vectors for reduce phase

    atomicJobStage* atomicStage;
    int processedKeys;   /// TODO - should be atomic?
    JobState state;   /// TODO should we change to an atomic var?
    atomicIntCounter indexCounter;  //
    std::atomic<int> nextPhaseInputSize; //

    pthread_mutex_t mutex; //
    pthread_mutex_t emitMutex;  /// TODO - is possible with one mutex?
};

///-------------------------------- free resources ------------------------------------
/**
 * Frees the given array of threads contexts
 * @param threads- the array of thread contexts to free
 * @param len- the length of the array
 */
void freeThreadContexts(ThreadContext* threads, int len) {
    for(int i = 0; i < len; i++) {
        delete (threads + i)->intermediateVec;
        delete (threads + i)->thread;
        // TODO do we need to delete pthread?
    }
}
/**
 * Frees the given job
 * @param job- the job to free
 */
void freeJobContext(JobContext* job) {
    std::cout << "start free job\n";
    freeThreadContexts(job->threadContexts, job->multiThreadLevel);
// delete job->indexCounter;
    delete job->barrier;
    delete job->atomicStage;
    if (pthread_mutex_destroy(&job->mutex) != 0) {
        std::cerr << MUTEX_DESTROY_ERR;
        exit(EXIT_FAILURE);
    }
}

///-------------------------------- handle errors ------------------------------------
/**
 * Handles with allocation failure by printing an error and exit the program
 */
void allocation_failure(){
    std::cerr << ERROR_MESSAGE_ALLOCATION_FAILURE << std::endl;
    exit(EXIT_FAILURE);
}

 /**
 * Handles with allocation of mutex functionality
  * @param the job of the current mutex
  */
void mutex_failure(JobContext* job, bool toLock){
    freeJobContext(job);
    if (toLock){std::cerr << MUTEX_LOCK_FAILURE << std::endl;}
    else{std::cerr << MUTEX_UNLOCK_FAILURE << std::endl;}
    exit(EXIT_FAILURE);
}
///-------------------------------- create structs ------------------------------------
/**
 * Creates a new instance of thread context
 * @param jobContext- the job that creates the current thread
 * @param id- the id of the new thread
 * @return- a pointer of the new thread context
 */
ThreadContext* createThreadContext(JobContext* jobContext, int id)
{
    auto* threadContext = new (std::nothrow) ThreadContext;
    if (!threadContext) {allocation_failure();}

    threadContext->thread = new (std::nothrow) pthread_t;
    threadContext->intermediateVec = new (std::nothrow) IntermediateVec;
    threadContext->job = jobContext;
    threadContext->id = id;

    if (!threadContext->thread || !threadContext->intermediateVec)
    {   // allocation failed
        delete threadContext->thread;
        delete threadContext->intermediateVec;
        delete threadContext;
        allocation_failure();
    }
    return threadContext;
}

/**
 * Creates a new instance of job context
 * @param threads- a pointer for the array of threads contexts of the current job
 * @param multiThreadLevel- the number of threads of the current job
 * @param inputVec- the input vector of the current job
 * @param outputVec- the output vector of the current job that will be updated after the reduce phase
 * @param client- the client of the curent job
 * @return a pointer of the new job context
 */
JobContext* createJobContext(ThreadContext* threads, int multiThreadLevel, const InputVec& inputVec,
                             OutputVec& outputVec, const MapReduceClient& client)
{
    JobContext* jobContext = new (std::nothrow) JobContext;
    if (!jobContext) {allocation_failure();}
    jobContext->multiThreadLevel = multiThreadLevel;
    jobContext->threadContexts = threads;
    jobContext->client = &client;
    jobContext->inputVec = &inputVec;
    jobContext->outputVec = &outputVec;

    jobContext->atomicStage = new (std::nothrow) atomicJobStage(0);
    jobContext->processedKeys = 0;
    jobContext->isJoined = false;

    jobContext->allIntermediateVecs =  IntermediateVecsVector(multiThreadLevel);  //TODO - is this good?
    jobContext->indexCounter = 0;
    jobContext->mutex = pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER);
    jobContext->emitMutex = pthread_mutex_t(PTHREAD_MUTEX_INITIALIZER);

    jobContext->barrier = new Barrier(multiThreadLevel);
//    jobContext->shuffledVec = std::vector<IntermediateVec>(multiThreadLevel);  //TODO change names, add checking of allocation
    jobContext->vecToReduce = std::vector<IntermediateVec>();
    jobContext->nextPhaseInputSize = 0;

    // Check for allocation failures
    if ( !jobContext->barrier || !jobContext->atomicStage) {
        freeJobContext(jobContext);
        allocation_failure();
    }
    return jobContext;
}

///-------------------------- check and update stage --------------------------
stage_t getStage(JobContext* job)
{
    auto number = (*(job->atomicStage)).load();
    uint64_t stage = (number >> 62);  // Extract the stage bits
    return static_cast<stage_t>(stage);
}

// left  2 bits     31 bits            31 bits          right
        // stage    total keys      processed keys
void updateNewStage(JobContext* job, int stage, int total){
    job->indexCounter = 0;
    uint64_t jobStageBits = static_cast<uint64_t>(stage) << 62;
    uint64_t totalKeysBits = (static_cast<uint64_t>(total) & (0x7fffffffULL)) << 31;
    uint64_t processedKeysBits = ~(0x7fffffffULL);
    uint64_t updatedNumber = (jobStageBits | totalKeysBits) & processedKeysBits;
    (*(job->atomicStage)).store(updatedNumber); // Save the new stage
    std::bitset<64> bitset(updatedNumber);
}

void incrementProcessedKeysBy(JobContext* job, int factor){
    uint64_t number = job->atomicStage->load();

    std::bitset<64> bitset(number);

    uint64_t processedKeysMask = 0x7fffffffULL;  // Mask for the processed keys (31 bits set to 1)
    uint64_t processedKeys = (number & processedKeysMask);  // Extract the current processed keys
    processedKeys += factor;  // Increment the processed keys
    processedKeys &= processedKeysMask;  // Apply the mask to keep the processed keys within the range
    number &= ~(processedKeysMask);  // Clear the current processed keys in the number
    number |= (processedKeys);  // Update the number with the incremented processed keys
    (*(job->atomicStage)).store(number); // Save the new stage

    std::bitset<64> bitset2(number);
}

float getPercentage(JobContext* job){
    uint64_t number =  (*(job->atomicStage)).load();
    uint64_t processedKeys = (number << 33) >> 33;  // Extract the processed keys
    std::bitset<64> bitset(processedKeys);
    std::cout << "processed " << bitset << "\n";
    uint64_t totalKeys = (number << 2) >> 33;  // Extract the total keys
    std::bitset<64> bitset2(totalKeys);
    std::cout << "total " << bitset2 << "\n";

    if (totalKeys == 0) {
        // Handle the case where totalKeys is 0 to avoid division by zero
        return 0.0f;
    }

    float percentage = static_cast<float>(processedKeys) /  static_cast<float>(totalKeys)* 100.0f;
    return percentage;
}

int getProcessedKeysCounter(JobContext* job){
    uint64_t number =  (*(job->atomicStage)).load();
    uint64_t processedKeys = (number & 0x7FFFFFFF);  // Extract the processed keys
    return static_cast<int>(processedKeys);
}

///-------------------------------- phases ------------------------------------
/**
 * Represents the map phase of the job
 * @param thread- the current thread
 * @param job- the current job
 */
void mapPhase(ThreadContext* thread, JobContext* job)
{
    unsigned long totalKeys = job->inputVec->size();
    if (getStage(job) == UNDEFINED_STAGE) {updateNewStage(job, MAP_STAGE, totalKeys);}
    // int index = getProcessedKeysCounter(job);

    while(true){            // TODO add bool var instead of while true?
        unsigned long index = (job->indexCounter)++;
        if (index < totalKeys){
            auto pair = job->inputVec->at(index);
            job->client->map(pair.first, pair.second, thread);
            int result = pthread_mutex_lock(&job->mutex);
            if(result != 0){mutex_failure(job, true);}
            incrementProcessedKeysBy(job, 1);
            result = pthread_mutex_unlock(&job->mutex);
            if(result != 0){mutex_failure(job, false);}
        }
        else{break;}
    }
}
/**
 * Represents the map phase of the job
 * @param pair1
 * @param pair2
 * @return
 */
bool compare(IntermediatePair pair1, IntermediatePair pair2){
    return pair1.first->operator<(*pair2.first);
}

/**
 * Represents the shuffle phase of the job
 * @param job- the current job
 */
void shufflePhase(JobContext* job){
    IntermediateVec allPairsVec;
    for(const IntermediateVec& vec: (job->allIntermediateVecs)){
        allPairsVec.insert(allPairsVec.end(), vec.begin(), vec.end()); // combine  threads' vectors
    }
    if (getStage(job) == MAP_STAGE) {updateNewStage(job, SHUFFLE_STAGE, allPairsVec.size());}

    if(!allPairsVec.empty()) {
        std::sort(allPairsVec.begin(), allPairsVec.end(), compare);

        IntermediatePair prev_pair = {nullptr, nullptr};
        IntermediatePair curr_pair;

        while (!allPairsVec.empty()) {
            curr_pair = allPairsVec.back();
            if (prev_pair.first == nullptr || (prev_pair.first != nullptr && (compare(curr_pair, prev_pair)
                                                                              || compare(prev_pair,
                                                                                         curr_pair)))) // we are in the first pair or a pair with new key
            {
                IntermediateVec new_vec = IntermediateVec();  // create vec for cur key
                job->vecToReduce.insert(job->vecToReduce.begin(), new_vec);  // insert to reduce vec
            }
            job->vecToReduce[0].push_back(curr_pair);  // cur pair has the same key of recently created vec
            prev_pair = curr_pair;  // update
            allPairsVec.pop_back();  // pop all
            incrementProcessedKeysBy(job, 1);
        }
    }
}

/**
 * Represents the reduce phase of the job
 * @param job- the current job
 */
void reducePhase(ThreadContext* threadContext){
    JobContext* job = threadContext->job;
    if (getStage(job) == SHUFFLE_STAGE) {updateNewStage(job, REDUCE_STAGE, job->nextPhaseInputSize);}
    unsigned long vecToReduceSize = job->vecToReduce.size();

//    int result = pthread_mutex_lock(&job->mutex);
//    if(result != 0){mutex_failure(job, true);}
//    unsigned long index = (job->indexCounter)++;
//    result = pthread_mutex_unlock(&job->mutex);
//    if(result != 0){mutex_failure(job, false);}

    while(true){            // TODO add bool var instead of while true?
        unsigned long index = (job->indexCounter)++;
        if (index < vecToReduceSize){
            auto currentVector = job->vecToReduce[index];
            job->client->reduce(&currentVector, threadContext);
            int result = pthread_mutex_lock(&job->mutex);
            if(result != 0){mutex_failure(job, true);}
            incrementProcessedKeysBy(job, currentVector.size());
            result = pthread_mutex_unlock(&job->mutex);
            if(result != 0){mutex_failure(job, false);}
        }
        else{break;}
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
    std::sort((job->allIntermediateVecs)[thread->id].begin(), job->allIntermediateVecs[thread->id].end(), compare);
    waitForAllThreads(job);  // verify all threads finished map & sort
    if(thread->id == 0) {shufflePhase(job);}
    waitForAllThreads(job);  // verify thread 0 finished shuffle
    reducePhase(thread);
    return nullptr;
}


///------------------------------------------- library -------------------------------------------------------

void emit2(K2* key, V2* value, void* context)
{
    auto threadContext = (ThreadContext*) context;
    auto job = threadContext->job;
    int result = pthread_mutex_lock(&job->emitMutex);
    if(result != 0){mutex_failure(job, true);}
    IntermediatePair pair = IntermediatePair(key, value);
    (job->allIntermediateVecs)[threadContext->id].push_back(pair);
    result = pthread_mutex_unlock(&job->emitMutex);
    if(result != 0){mutex_failure(job, false);}
    job->nextPhaseInputSize++;
}

void emit3 (K3* key, V3* value, void* context){
    auto threadContext = (ThreadContext*) context;
    auto job = threadContext->job;
    int result = pthread_mutex_lock(&job->emitMutex);
    if(result != 0){mutex_failure(job, true);}
    OutputPair pair = OutputPair(key, value);
    job->outputVec->push_back(pair);
    result = pthread_mutex_unlock(&job->emitMutex);
    if(result != 0){mutex_failure(job, false);}
}

JobHandle startMapReduceJob(const MapReduceClient& client,
                            const InputVec& inputVec, OutputVec& outputVec, int multiThreadLevel)
{
    auto* threads = new(std::nothrow) ThreadContext[multiThreadLevel];
    JobContext* jobContext = createJobContext(threads, multiThreadLevel, inputVec, outputVec, client);
    for(int i = 0; i < multiThreadLevel; i++){
        threads[i] = *createThreadContext(jobContext, i);
        int result = pthread_create((threads + i)->thread, nullptr,mapReduce, threads +i);
        if(result != 0){
            freeJobContext(jobContext);
            allocation_failure();
        }
    }
    return jobContext;
}

void waitForJob(JobHandle job){
    // if isJoined = true, while we are not 100% and not in reduce, so we will check each time
    auto jobContext = static_cast<JobContext*>(job);
    if (jobContext->isJoined){
        JobState state;
        getJobState(jobContext, &state);
        while (state.stage != REDUCE_STAGE || state.percentage != 100.0){getJobState(jobContext, &state);}
    }
    else{
        jobContext->isJoined = true;
        for(int i = 0; i < jobContext->multiThreadLevel; i++){
            std::cout << "wait\n";
            int result = pthread_join(*(jobContext->threadContexts[i].thread), nullptr);
            if(result != 0)
            {
                freeJobContext(jobContext);
                allocation_failure();
            }
        }
    }
    std::cout << "finish\n";
}

void getJobState(JobHandle job, JobState* state){
    auto jobContext = static_cast<JobContext*>(job);
    state->stage = getStage(jobContext);
    state->percentage = getPercentage(jobContext);
}
void closeJobHandle(JobHandle job){
    std::cout << "start close job\n";
    waitForJob(job);
    std::cout << "after wait\n";
    auto jobContext = static_cast<JobContext*>(job);
    freeJobContext(jobContext);
}

