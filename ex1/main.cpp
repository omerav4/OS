#include <iostream>
#include "osm.cpp" // Include the header file for osm.cpp

int main() {
    double operation_time = osm_operation_time(10000000000000000000);
//    int function_time = osm_function_time();
//    int syscall_time = osm_syscall_time();
    std::cout << "Operation time: " << operation_time << std::endl;
//    std::cout << "Function tie: " << function_time << std::endl;
//    std::cout << "Syscall time" << syscall_time << std::endl;
    return 0;
}