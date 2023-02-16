cmake_minimum_required (VERSION 3.8)

project ("project3D")

set (CMAKE_WIN32_EXECUTABLE "True")
set (CMAKE_CXX_FLAGS 
	"/Wall /std:c++20 /DUNICODE /TP /Zc:__cplusplus /EHs")

add_subdirectory ("project3D")
