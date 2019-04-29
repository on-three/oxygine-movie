cmake_minimum_required (VERSION 2.6)
project (OXYGINEMOVIE)

set(OXYGINEMOVIE_INCLUDE_DIRS
	${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/deps
	)

if (NOT EMSCRIPTEN)
  set(OXYGINEMOVIE_INCLUDE_DIRS ${OXYGINEMOVIE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/libtheora/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../oxygine-sound/dependencies/tremor/Tremor
    ${CMAKE_CURRENT_SOURCE_DIR}/../oxygine-sound/dependencies/libogg/include
  )
else()
  set(OXYGINEMOVIE_INCLUDE_DIRS ${OXYGINEMOVIE_INCLUDE_DIRS}
	  ${CMAKE_CURRENT_SOURCE_DIR}/src/web
  )
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

message(status ${OXYGINEMOVIE_INCLUDE_DIRS})

set(SRC
	src/MovieSprite.h
	src/MovieSprite.cpp
	)

if (NOT EMSCRIPTEN)
  set (SRC ${SRC}
    src/theora/MovieSpriteTheora.cpp
    deps/libtheora/lib/analyze.c
    deps/libtheora/lib/apiwrapper.c
    deps/libtheora/lib/bitpack.c
    deps/libtheora/lib/cpu.c
    deps/libtheora/lib/decapiwrapper.c
    deps/libtheora/lib/decinfo.c
		deps/libtheora/lib/decode.c
    deps/libtheora/lib/dequant.c
    deps/libtheora/lib/encapiwrapper.c
    deps/libtheora/lib/encfrag.c
    deps/libtheora/lib/encinfo.c
    deps/libtheora/lib/encode.c
    deps/libtheora/lib/enquant.c
    deps/libtheora/lib/fdct.c
    deps/libtheora/lib/fragment.c
    deps/libtheora/lib/huffdec.c
    deps/libtheora/lib/huffenc.c
    deps/libtheora/lib/idct.c
    deps/libtheora/lib/info.c
    deps/libtheora/lib/internal.c
    deps/libtheora/lib/mathops.c
    deps/libtheora/lib/mcenc.c
    deps/libtheora/lib/quant.c
    deps/libtheora/lib/rate.c
    deps/libtheora/lib/state.c
    deps/libtheora/lib/tokenize.c
    )
else()
  set (SRC ${SRC} src/web/MovieSpriteWeb.cpp) 
endif()

add_library(oxygine-movie STATIC ${SRC})

if (WIN32)
	set_target_properties(oxygine-movie PROPERTIES COMPILE_FLAGS "/wd4554 /wd4018 /wd4554 /wd4700 /wd4244 /wd4018 /wd4700 /wd4101")
endif(WIN32)


set(OXYGINEMOVIE_INCLUDE_DIRS ${OXYGINEMOVIE_INCLUDE_DIRS}
	${CMAKE_CURRENT_SOURCE_DIR}/dependencies/openal/include)

set(OXYGINEMOVIE_LIBS
	oxygine-movie
  tremor
  -logg
	)

if (NOT EMSCRIPTEN)
  set(OXYGINEMOVIE_LIBS ${OXYGINEMOVIE_LIBS}
    tremor
    -logg
  )
endif()

include_directories(${OXYGINEMOVIE_INCLUDE_DIRS})


set(OXYGINEMOVIE_INCLUDE_DIRS
	${CMAKE_CURRENT_SOURCE_DIR}/src)

set(OXYGINEMOVIE_INCLUDE_DIRS ${OXYGINEMOVIE_INCLUDE_DIRS} PARENT_SCOPE)
set(OXYGINEMOVIE_LIBS ${OXYGINEMOVIE_LIBS} PARENT_SCOPE)
