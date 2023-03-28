#include <iostream>
#include "osm.h" // Include the header file for osm.cpp

int main() {
    double operation_time = osm_operation_time(1000000);
    double function_time = osm_function_time(1000000);
    double syscall_time = osm_syscall_time(1000000);
    std::cout << "Operation time: " << operation_time << std::endl;
    std::cout << "Function time: " << function_time << std::endl;
    std::cout << "Syscall time" << syscall_time << std::endl;
    return 0;
}