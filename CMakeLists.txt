cmake_minimum_required(VERSION 3.16)
project(ITCHFeedHandler 
    VERSION 1.0.0
    DESCRIPTION "High-Performance NASDAQ ITCH 5.0 Market Data Feed Handler"
    LANGUAGES CXX
)

# =============================================================================
# C++ Standard
# =============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Build Type
# =============================================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# =============================================================================
# Compiler Flags
# =============================================================================

# Platform-specific optimizations
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # GCC/Clang optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    
    # Additional optimization flags for release
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(
            -march=native           # Target current CPU
            -mtune=native           # Tune for current CPU
            -ffast-math             # Fast floating point
            -funroll-loops          # Loop unrolling
            -fomit-frame-pointer    # Free up a register
        )
        
        # Link-time optimization (LTO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    
    # Warnings
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    
elseif(MSVC)
    # MSVC optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od")
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(
            /Oi     # Enable intrinsics
            /Ot     # Favor fast code
            /GL     # Whole program optimization
            /arch:AVX2  # Use AVX2 if available
        )
        
        # Link-time code generation
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
    endif()
    
    # Warnings
    add_compile_options(/W4)
    
    # Disable specific warnings
    add_compile_options(/wd4127)  # conditional expression is constant
endif()

# =============================================================================
# Include Directories
# =============================================================================

include_directories(${CMAKE_SOURCE_DIR}/include)

# =============================================================================
# Header-Only Library (for use in other projects)
# =============================================================================

add_library(itch_feed_handler INTERFACE)
target_include_directories(itch_feed_handler INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(itch_feed_handler INTERFACE cxx_std_17)

# =============================================================================
# Main Executable (Benchmark)
# =============================================================================

add_executable(itch_benchmark src/main.cpp)
target_link_libraries(itch_benchmark PRIVATE itch_feed_handler)

# Platform-specific libraries
if(WIN32)
    # Windows: for memory mapping
elseif(UNIX)
    target_link_libraries(itch_benchmark PRIVATE pthread)
endif()

# =============================================================================
# Tests
# =============================================================================

enable_testing()

# Parser tests
add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE itch_feed_handler)
add_test(NAME ParserTests COMMAND test_parser)

# Order book tests
add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE itch_feed_handler)
add_test(NAME OrderBookTests COMMAND test_order_book)

# Common unit test (isolation check)
add_executable(test_common src/test_common.cpp)
target_link_libraries(test_common PRIVATE itch_feed_handler)

# =============================================================================
# Install
# =============================================================================

install(FILES
    include/common.hpp
    include/message_types.hpp
    include/itch_parser.hpp
    include/order_book.hpp
    include/feed_handler.hpp
    DESTINATION include/itch
)

install(TARGETS itch_feed_handler
    EXPORT ITCHFeedHandlerTargets
)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== ITCH Feed Handler Build Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
