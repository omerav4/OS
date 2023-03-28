#include "osm.h"
#include <sys/time.h>
#include <cmath>
#define UNROLLING_LOOP_FACTOR 5

double osm_operation_time(unsigned int iterations){
    int a = 1;
    if(iterations == 0){
        return -1;
    }
    timeval start_time, end_time;
    unsigned int iters = (unsigned int)(std::ceil(iterations/UNROLLING_LOOP_FACTOR));
    if(gettimeofday(&start_time, nullptr) == -1){
        return -1;
    }
    for (unsigned int i = 0; i < iters; i++) {
        a = a + 1;
        a = a + 1;
        a = a + 1;
        a = a + 1;
        a = a + 1;
    }
    if(gettimeofday(&end_time, nullptr) == -1){
        return -1;
    }
    double passed_time = (double)(end_time.tv_sec - start_time.tv_sec) * 1e9 +
                                            (end_time.tv_usec - start_time.tv_usec) * 1e3;
    return passed_time / (iters * UNROLLING_LOOP_FACTOR);
}

void empty_helper(){
    return;
}

double osm_function_time(unsigned int iterations){
    if(iterations == 0){
        return -1;
    }
    timeval start_time, end_time;
    unsigned int iters = (unsigned int)(std::ceil(iterations/UNROLLING_LOOP_FACTOR));
    if(gettimeofday(&start_time, nullptr) == -1){
        return -1;
    }
    for (unsigned int i = 0; i < iters; i++) {
        empty_helper();
        empty_helper();
        empty_helper();
        empty_helper();
        empty_helper();
    }
    if(gettimeofday(&end_time, nullptr) == -1){
        return -1;
    }
    double passed_time = (double)(end_time.tv_sec - start_time.tv_sec) * 1e9 +
                            (end_time.tv_usec - start_time.tv_usec) * 1e3;
    return passed_time / (iters * UNROLLING_LOOP_FACTOR);
}


double osm_syscall_time(unsigned int iterations){
    if(iterations == 0){
        return -1;
    }
    timeval start_time, end_time;
    unsigned int iters = (unsigned int)(std::ceil(iterations/UNROLLING_LOOP_FACTOR));
    if(gettimeofday(&start_time, nullptr) == -1){
        return -1;
    }
    for (unsigned int i = 0; i < iters; i++) {
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
    }
    if(gettimeofday(&end_time, nullptr) == -1){
        return -1;
    }
    double passed_time = (double)(end_time.tv_sec - start_time.tv_sec) * 1e9 +
                            (end_time.tv_usec - start_time.tv_usec) * 1e3;
    return passed_time / (iters * UNROLLING_LOOP_FACTOR);
}










