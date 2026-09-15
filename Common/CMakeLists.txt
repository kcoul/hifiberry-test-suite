project(HiFiBerryTestSuiteCommon VERSION 0.1)

# INTERFACE so each app compiles these sources against its own copy of the JUCE modules.
add_library(${PROJECT_NAME} INTERFACE)

target_sources(${PROJECT_NAME} INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/Analysis.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AudioClip.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Comparison.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Devices.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Report.cpp)

target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
