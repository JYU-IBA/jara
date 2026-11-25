# Version information from git. Releases are tagged as vX.Y.Z. version.txt holds the same number for builds without
# git (e.g. from a source archive), release_scripts/bump_version.sh keeps the two in sync.
#
# When included from CMakeLists.txt this sets JARA_VERSION_TAG to the numeric version of the latest release tag, or
# to the version in version.txt when there is no tag.
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
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/version.txt")
    file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" version_file)
    string(STRIP "${version_file}" version_file)
    jara_git(tag describe --tags --abbrev=0 --match "v[0-9]*")
    if(tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
        set(JARA_VERSION_TAG "${CMAKE_MATCH_1}")
        if(NOT JARA_VERSION_TAG STREQUAL version_file)
            message(WARNING "Latest git tag ${tag} does not match version.txt (${version_file})")
        endif()
    else()
        set(JARA_VERSION_TAG "${version_file}")
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
# The description as a semantic version: the number of the tag, and what git describe adds after it as build metadata,
# which does not change the precedence: v0.2.1 is 0.2.1, v0.2.1-dirty 0.2.1+dirty and v0.2.1-3-gabcdef0-dirty
# 0.2.1+3.gabcdef0.dirty. A suffix after "-" would make it a pre-release, which comes before 0.2.1.
set(JARA_SEMVER "")
if(GIT_DESCRIBE MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)(-(.*))?$")
    set(JARA_SEMVER "${CMAKE_MATCH_1}")
    if(CMAKE_MATCH_3)
        string(REPLACE "-" "." metadata "${CMAKE_MATCH_3}")
        string(APPEND JARA_SEMVER "+${metadata}")
    endif()
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
