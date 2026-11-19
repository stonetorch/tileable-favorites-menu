cmake_minimum_required(VERSION 3.21)
project(TFMEGridTests LANGUAGES CXX)

enable_testing()
add_executable(tfme_grid_tests GridTests.cpp ../src/Grid.cpp)
target_include_directories(tfme_grid_tests PRIVATE ../src)
target_compile_features(tfme_grid_tests PRIVATE cxx_std_20)
add_test(NAME tfme_grid COMMAND tfme_grid_tests)
