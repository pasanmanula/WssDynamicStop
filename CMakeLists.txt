cmake_minimum_required(VERSION 3.5)
project(stock_marker)

set(CMAKE_CXX_STANDARD 17)

# API KEY PATH
set(CONFIG_FILE_PATH "${CMAKE_SOURCE_DIR}/config/finnhub_api_key.txt")
add_definitions(-DCONFIG_FILE_PATH=\"${CONFIG_FILE_PATH}\")

# Stock list path
set(STOCK_LIST_FILE_PATH "${CMAKE_SOURCE_DIR}/config/stock_list.json")
add_definitions(-DSTOCK_LIST_FILE_PATH=\"${STOCK_LIST_FILE_PATH}\")

# Add definition for ASIO_STANDALONE
add_definitions(-DASIO_STANDALONE)

# Include WebSocket++ headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Add executable
add_executable(${PROJECT_NAME} src/main.cpp)

# Find OpenSSL
find_package(OpenSSL REQUIRED)
if (OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES})
endif()

# Link OpenSSL
target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES})

# Find Boost - Need for Lock-Free Queue
find_package(Boost REQUIRED COMPONENTS system thread)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
else()
    message(FATAL_ERROR "Boost not found!")
endif()

# Link Boost libraries
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES})


# Enable threading support
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)