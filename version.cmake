# Version information from git. Releases are tagged as vX.Y.Z, like in JaBS.
#
# When included from CMakeLists.txt this sets JARA_VERSION_TAG to the numeric version of the latest release tag.
# When run as a script (cmake -P) it writes VERSION_OUTPUT from VERSION_INPUT with the state of the git repository,
# which is done on every build so that the version and dirtiness of the working tree stay up to date.

find_package(Git QUIET)

function(jara_git OUTPUT)
    set(${OUTPUT} "" PARENT_SCOPE)
    if(NOT GIT_EXECUTABLE)
        return()
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" ${ARGN}
            WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(result EQUAL 0)
        set(${OUTPUT} "${output}" PARENT_SCOPE)
    endif()
endfunction()

if(NOT CMAKE_SCRIPT_MODE_FILE)
    jara_git(tag describe --tags --abbrev=0 --match "v[0-9]*")
    if(tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
        set(JARA_VERSION_TAG "${CMAKE_MATCH_1}")
    else()
        set(JARA_VERSION_TAG "0.0.0")
        message(WARNING "No version tag (vX.Y.Z) found in git, using version ${JARA_VERSION_TAG}")
    endif()
    message(STATUS "JaRA version is ${JARA_VERSION_TAG}")
    return()
endif()

jara_git(GIT_DESCRIBE describe --tags --match "v[0-9]*" --dirty)
jara_git(GIT_HEAD_SHA1 rev-parse HEAD)
jara_git(GIT_BRANCH rev-parse --abbrev-ref HEAD)
jara_git(GIT_COMMIT_DATE_ISO8601 log -1 --format=%cI)
if(GIT_DESCRIBE AND GIT_HEAD_SHA1)
    set(GIT_RETRIEVED_STATE true)
else()
    set(GIT_RETRIEVED_STATE false)
endif()
jara_git(status status --porcelain --untracked-files=no)
if(status)
    set(GIT_IS_DIRTY true)
else()
    set(GIT_IS_DIRTY false)
endif()
configure_file("${VERSION_INPUT}" "${VERSION_OUTPUT}" @ONLY) # Only touches the output when the content changes
