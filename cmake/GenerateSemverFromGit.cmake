find_package(Git QUIET REQUIRED)

function(GenerateSemverInfo)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} diff --shortstat
        COMMAND tail -n1
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --exact-match --tags HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE IS_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        OUTPUT_QUIET
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if((IS_TAG EQUAL 0) AND (GIT_DIRTY STREQUAL ""))
        set(${PROJECT_NAME}_VERSION_TWEAK "")
    else()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list HEAD --count
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_REV_COUNT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GIT_REV_COUNT STREQUAL "")
            set(${PROJECT_NAME}_VERSION_TWEAK "-unknown")
        else()
            set(${PROJECT_NAME}_VERSION_TWEAK "-${GIT_REV_COUNT}")
        endif()
    endif()
    
    if(GIT_DIRTY STREQUAL "")
        set(${PROJECT_NAME}_VERSION_DIRTY "")
    else()
        set(${PROJECT_NAME}_VERSION_DIRTY ".dirty")
    endif()

    set(${PROJECT_NAME}_BUILDINFO "+${GIT_HASH}${${PROJECT_NAME}_VERSION_DIRTY}")
    set(${PROJECT_NAME}_LAST ${${PROJECT_NAME}_VERSION_TWEAK}${${PROJECT_NAME}_BUILDINFO} PARENT_SCOPE)
endfunction()
