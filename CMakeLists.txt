cmake_minimum_required(VERSION 2.8)
project(freetype-test C)

set(CMAKE_C_STANDARD 99)
#set(CMAKE_C_FLAGS "-fshort-wchar")

if (CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
	find_package(OpenCV REQUIRED)
	include_directories(${OpenCV_INCLUDE_DIRS})
	link_directories(${OpenCV_LIBRARY_DIRS})
	link_libraries(${OpenCV_LIBS})
	add_definitions("-DOPEN_CV=1")
endif ()

include_directories("/usr/local/include/freetype2")
link_libraries(freetype m)
add_executable(${PROJECT_NAME} main.c)
