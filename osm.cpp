#include "osm.h"
#include <sys/time.h>
#define LO0P_ROLLING_NUM = 5

double osm_operation_time(unsigned int iterations){
    int a = 1;
    if(iterations == 0){
        return -1;
    }
    timeval start_time, end_time;
    if(gettimeofday(&start_time, nullptr) == -1){
        return -1;
    }
    for (unsigned int i = 0; i < (iterations / 5); i++) {
        a = a + 1;
        a = a + 1;
        a = a + 1;
        a = a + 1;
        a = a + 1;
    }
    if(gettimeofday(&end_time, nullptr) == -1){
        return -1;
    }
    long long passed_time = (end_time.tv_sec - start_time.tv_sec) * 1000000000LL +
                                            (end_time.tv_usec - start_time.tv_usec) * 1000LL;
    return passed_time / (iterations * 5);
}

//double osm_function_time(unsigned int iterations){
//    if(iterations == 0){
//        return -1;
//    }
//    timeval start_time, end_time;
//    if(gettimeofday(&start_time, nullptr) == -1){
//        return -1;
//    }
//    for (unsigned int i = 0; i < iterations; i++) {
//        empty_helper();
//        empty_helper();
//        empty_helper();
//        empty_helper();
//        empty_helper();
//    }
//    if(gettimeofday(&end_time, nullptr) == -1){
//        return -1;
//    }
//    long long passed_time = (end_time.tv_sec - start_time.tv_sec) * 1000000000LL +
//                            (end_time.tv_usec - start_time.tv_usec) * 1000LL;
//    return passed_time / (iterations * LO0P_ROLLING_NUM);
//}
//
//
//double osm_syscall_time(unsigned int iterations){
//    if(iterations == 0){
//        return -1;
//    }
//    timeval start_time, end_time;
//    if(gettimeofday(&start_time, nullptr) == -1){
//        return -1;
//    }
//    for (unsigned int i = 0; i < iterations; i++) {
//        OSM_NULLSYSCALL
//        OSM_NULLSYSCALL
//        OSM_NULLSYSCALL
//        OSM_NULLSYSCALL
//        OSM_NULLSYSCALL
//    }
//    if(gettimeofday(&end_time, nullptr) == -1){
//        return -1;
//    }
//    long long passed_time = (end_time.tv_sec - start_time.tv_sec) * 1000000000LL +
//                            (end_time.tv_usec - start_time.tv_usec) * 1000LL;
//    return passed_time / (iterations * LO0P_ROLLING_NUM);
//}

void empty_helper(){
    return;
}









