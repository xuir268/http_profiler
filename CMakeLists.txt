cmake_minimum_required(VERSION 3.16)
project(profiler-server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Boost headers (Beast is header-only; boost::system is header-only >=1.70)
find_package(Boost 1.70 REQUIRED)

# Find Threads
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/http_server.cpp
    src/profile.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Boost::headers
    Threads::Threads
)

# macOS specific (if using Homebrew)
if(APPLE)
    target_include_directories(${PROJECT_NAME} PRIVATE /opt/homebrew/include)
    target_link_directories(${PROJECT_NAME} PRIVATE /opt/homebrew/lib)
endif()
