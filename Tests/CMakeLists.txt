#######################################################################
# HCoreLib host tests.
#
# A standalone CMake project, built with whatever compiler the machine has:
#
#   cmake -S Tests -B build-tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
#
# It pulls HCoreLib in as an ordinary subdirectory - the same sources the
# firmware is built from, not a reimplementation of them - and points it at the
# test-owned Config/ rather than an application's. Everything that is target-
# only (HTask, HTaskManager, Devices/, Portal/) is left out of the host build by
# HCoreLib's own CMakeLists, which is exactly why those modules have no suite
# here.
#######################################################################

cmake_minimum_required(VERSION 3.16)
project(HCoreLibTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cached, because HCoreLib's CMakeLists reads it from its own directory scope.
set(HCORELIB_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/Config" CACHE INTERNAL
    "Directory holding the test build's HCoreLibConfig.h")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." HCoreLib)

add_executable(hcorelibtests
    "HCoreLibTest.cpp"
    "HValueTest.cpp"
    "HJsonTest.cpp"
    "HConfigPathTest.cpp"
    "HConfigTest.cpp"
    "HLogTest.cpp"
    "HSha256Test.cpp"
    "HTimerTest.cpp"
    "HHookListTest.cpp"
    "HFsTest.cpp"
    "HGpioTest.cpp"
    "HButtonTest.cpp"
    "HRtcStoreTest.cpp"
    "HAuthTest.cpp")

target_link_libraries(hcorelibtests PRIVATE HCoreLib)

if(MSVC)
  target_compile_options(hcorelibtests PRIVATE /W4)
else()
  target_compile_options(hcorelibtests PRIVATE -Wall -Wextra)
endif()

# HConfig and HFs address the filesystem relative to the working directory, so
# the suite creates config/ and fstest/ wherever it is started from. Running it
# in its own directory keeps those out of the source tree and out of whatever
# directory a developer happened to be standing in.
enable_testing()
add_test(NAME hcorelibtests COMMAND hcorelibtests)
set_tests_properties(hcorelibtests PROPERTIES
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
