if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(WARN_SWITCHES "-Wall -Wextra -Werror -Wshadow -Winit-self -Wcast-qual -Wcast-align -Wformat=2 -Wwrite-strings -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wstrict-overflow=5 -Wdisabled-optimization -Wmissing-include-dirs -Wswitch-default -Wundef -pedantic")

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_C_COMPILER_VERSION GREATER_EQUAL 4.5.0)
            set(WARN_SWITCHES "${WARN_SWITCHES} -Wunused-result")
        endif()
    endif()

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_C_COMPILER_VERSION GREATER_EQUAL 7.0.0)
            set(WARN_SWITCHES "${WARN_SWITCHES} -Wformat-truncation -Wstringop-overflow -Wformat-overflow=2")
        endif()
        if(CMAKE_C_COMPILER_VERSION GREATER_EQUAL 8.0.0)
            set(WARN_SWITCHES "${WARN_SWITCHES} -Wstringop-truncation")
        endif()
    endif()

    set(CMAKE_C_FLAGS "-std=c11 -pipe -fno-common ${WARN_SWITCHES} ${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS_RELEASE "-fno-asynchronous-unwind-tables ${CMAKE_C_FLAGS_RELEASE}")
endif()

if(MSVC)
  if(CMAKE_C_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
  endif()
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4200 /wd4201 /wd4204 /wd4221 /wd4702")
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wdocumentation")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

