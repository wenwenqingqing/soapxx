# CONFIGURE CMAKE
message("CMake version: ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 2.8.3)

# PROJECT OPTIONS
project(soapxx)
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR})
set(LOCAL_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/soap)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDBG")

# BUILD OPTIONS
enable_language(CXX)
#set(CMAKE_CXX_COMPILER "/usr/local/shared/intel/compilers_and_libraries_2018/linux/bin/intel64/icc")
message("C++ compiler: " ${CMAKE_CXX_COMPILER} " " ${CMAKE_CXX_COMPILER_ID})
option(BUILD_SHARED_LIBS "Build shared libs" ON)
if(${CMAKE_VERSION} VERSION_GREATER 3.1)
    message("Setting C++ standard 11 (CMake version > 3.1)")
    set(CMAKE_CXX_STANDARD 11)
else(${CMAKE_VERSION} VERSION_GREATER 3.1)
    message("Setting C++ standard 11 (CMake version <= 3.1)")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif(${CMAKE_VERSION} VERSION_GREATER 3.1)

# LOCAL PACKAGES
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# FIND PACKAGES
find_package(PythonLibs 2.7)
include_directories(${PYTHON_INCLUDE_DIRS})

if (${WITH_SYSTEM_BOOST} STREQUAL true)
    if(DEFINED ENV{BOOST_ROOT})
        set(BOOST_ROOT "$ENV{BOOST_ROOT}")
        message("-- BOOST_ROOT is set: ${BOOST_ROOT}")
    else(DEFINED ENV{BOOST_ROOT})
        message("-- Note: BOOST_ROOT not set. Let's see whether we can find BOOST anyway.")
    endif(DEFINED ENV{BOOST_ROOT})
    find_package(Boost COMPONENTS python serialization)
    include_directories(${Boost_INCLUDE_DIRS})
else()
    set(Boost_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/soap/include")
    set(Boost_LIBRARIES 
     "${CMAKE_CURRENT_SOURCE_DIR}/soap/lib/libboost_python.so"
     "${CMAKE_CURRENT_SOURCE_DIR}/soap/lib/libboost_serialization.so"
    )
    include_directories(${Boost_INCLUDE_DIRS})
endif()
set(ignoreMe $WITH_SYSTEM_BOOST) # Needed to suppress cmake warning

include_directories("/home/capoe/packages/intel/mkl/include")
set(MKL_INCLUDE_DIR "/home/capoe/packages/intel/mkl/include")

set(SOAPXX_LINK_LIBRARIES ${Boost_LIBRARIES} ${PYTHON_LIBRARIES} ${GSL_LIBRARIES})

# SUMMARIZE INCLUDES & LIBS
get_property(local_dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
get_property(local_libs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY SOAPXX_LINK_LIBRARIES)

message(STATUS "Include directories: ")
foreach(dir ${local_dirs})
  message(STATUS " o ${dir}")
endforeach()
message(STATUS "Linked libraries:    ")
foreach(lib ${local_dirs})
  message(STATUS " o ${lib}")
endforeach()

# SUBDIRECTORIES
add_subdirectory(pybind11)
add_subdirectory(src/soap)
add_subdirectory(tools)

