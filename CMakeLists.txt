cmake_minimum_required(VERSION 3.0)
project(picewise VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(Catch2 REQUIRED)

add_library(picewise INTERFACE)

target_include_directories(picewise
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<INSTALL_INTERFACE:include/mz>)

SET(X ${CMAKE_CURRENT_SOURCE_DIR}/test)
add_executable(test ${X}/basic_aggregate.cpp;${X}/main.cpp;${X}/multifail.cpp;${X}/tuple_list.cpp)
target_link_libraries(test PRIVATE Catch2::Catch2;picewise)

# Create an install target
install(TARGETS picewise
        EXPORT picewise-config
        INCLUDES DESTINATION include)

install(EXPORT picewise-config
        FILE picewise-config.cmake
        NAMESPACE picewise::
        DESTINATION share/picewise/cmake)

install(DIRECTORY include/mz
        DESTINATION include FILES_MATCHING PATTERN "*.hpp")