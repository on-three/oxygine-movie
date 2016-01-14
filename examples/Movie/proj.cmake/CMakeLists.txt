cmake_minimum_required (VERSION 2.6)
project (Movie)

add_subdirectory(../../../../oxygine-framework oxygine-framework)

add_definitions(${OXYGINE_DEFINITIONS})
include_directories(${OXYGINE_INCLUDE_DIRS})
link_directories(${OXYGINE_LIBRARY_DIRS})

add_subdirectory(../../../../oxygine-sound oxygine-sound)
include_directories(${OXYGINESOUND_INCLUDE_DIRS})

add_subdirectory(../../../../oxygine-movie oxygine-movie)
include_directories(${OXYGINEMOVIE_INCLUDE_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

include_directories(../src)
add_executable(Movie
	../src/entry_point.cpp
	../src/example.cpp
	../src/example.h
	)

target_link_libraries(Movie
	${OXYGINE_CORE_LIBS} ${OXYGINESOUND_LIBS} ${OXYGINEMOVIE_LIBS})