cmake_minimum_required (VERSION 2.6)
project (Movie)

set(CMAKE_C_STANDARD 11)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wno-overloaded-virtual")

add_subdirectory(../../../../oxygine-framework oxygine-framework)

add_definitions(${OXYGINE_DEFINITIONS})
include_directories(${OXYGINE_INCLUDE_DIRS})
link_directories(${OXYGINE_LIBRARY_DIRS})

add_subdirectory(../../../../oxygine-sound oxygine-sound)
include_directories(${OXYGINESOUND_INCLUDE_DIRS})

add_subdirectory(../../../../oxygine-movie oxygine-movie)
include_directories(${OXYGINEMOVIE_INCLUDE_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OXYGINE_CXX_FLAGS}")

include_directories(../src)
add_executable(Movie
	../src/main.cpp
  ../src/example.cpp
	../src/example.h
  ../src/test.cpp
  ../src/TestCommon.h
  ../src/TestConverse.h
  ../src/TestStress.h
	)

target_link_libraries(Movie
	${OXYGINEMOVIE_LIBS} ${OXYGINE_CORE_LIBS} ${OXYGINESOUND_LIBS} )


if (EMSCRIPTEN)
	SET(CMAKE_EXECUTABLE_SUFFIX ".html")	
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --emrun -s NO_EXIT_RUNTIME=1 -s WARN_ON_UNDEFINED_SYMBOLS=1 -s ALLOW_MEMORY_GROWTH=1 -s -s FORCE_FILESYSTEM=1 --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../data/@/ -s 'EXTRA_EXPORTED_RUNTIME_METHODS=[\"ccall\", \"cwrap\"]'")
	em_link_pre_js(Movie  ${OXYGINE_JS_LIBRARIES} )
endif(EMSCRIPTEN)


if (WIN32) #disable console mode for VC++
	set_target_properties(Movie PROPERTIES WIN32_EXECUTABLE TRUE)
endif(WIN32)

