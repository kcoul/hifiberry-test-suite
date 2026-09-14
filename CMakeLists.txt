cmake_minimum_required(VERSION 3.28)

project(HiFiBerryTestSuite VERSION 0.1)

set(TargetName ${PROJECT_NAME})

add_subdirectory(JUCE)
add_subdirectory(GUI)
add_subdirectory(CLI)