cmake_minimum_required(VERSION 4.2)
include(version.cmake)
project(jara VERSION ${JARA_VERSION_TAG} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(GSL REQUIRED)
find_package(Jibal REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(jara
        src/main.cpp
        src/mainwindow.cpp
        src/mainwindow.h
        src/project.cpp
        src/project.h
        src/projectpanel.cpp
        src/projectpanel.h
        src/measurementinfoview.cpp
        src/measurementinfoview.h
        src/measurementview.cpp
        src/measurementview.h
        src/energyspectrum.cpp
        src/energyspectrum.h
        src/energyspectrumview.cpp
        src/energyspectrumview.h
        src/spectrumplotwidget.cpp
        src/spectrumplotwidget.h
        src/sampleview.cpp
        src/sampleview.h
        src/imageview.cpp
        src/imageview.h
        src/settingsview.cpp
        src/settingsview.h
        src/tabwidget.cpp
        src/tabwidget.h
        src/histogramwidget.cpp
        src/histogramwidget.h
        src/tofedata.cpp
        src/tofedata.h
        src/elements.cpp
        src/elements.h
        src/elementdialog.cpp
        src/elementdialog.h
        src/colorbutton.cpp
        src/colorbutton.h
        src/defaults.cpp
        src/defaults.h
        src/settingseditors.cpp
        src/settingseditors.h
        src/preferencesdialog.cpp
        src/preferencesdialog.h
        src/units.h
        src/projectimportdialog.cpp
        src/projectimportdialog.h
        src/projectinfoview.cpp
        src/projectinfoview.h
        src/tofcalibration.cpp
        src/tofcalibration.h
        src/tofcalibrationwindow.cpp
        src/tofcalibrationwindow.h
        src/anglecalibration.cpp
        src/anglecalibration.h
        src/anglecalibrationwindow.cpp
        src/anglecalibrationwindow.h
        src/calibrationplotwidget.cpp
        src/calibrationplotwidget.h
        src/nuclearstopping.cpp
        src/nuclearstopping.h
        src/stopping.cpp
        src/stopping.h
        src/materialeditor.cpp
        src/materialeditor.h
        src/matrix.h
        src/crosssection.cpp
        src/crosssection.h
        src/foildialog.cpp
        src/formlayout.cpp
        src/formlayout.h
        src/foildialog.h
        src/depthprofile.cpp
        src/depthprofile.h
        src/depthprofileview.cpp
        src/depthprofileview.h
        src/rangewidget.cpp
        src/reportpdf.cpp
        src/reportpdf.h
        src/reportview.cpp
        src/reportview.h
        src/rangewidget.h
        src/split.cpp
        src/tableexport.cpp
        src/tableexport.h
        src/split.h
        src/splitview.cpp
        src/splitview.h
        src/efficiencydialog.cpp
        src/efficiencydialog.h
        src/archive.cpp
        src/archive.h
        src/attachedfiletable.cpp
        src/attachedfiletable.h
        src/coincidence.cpp
        src/coincidence.h
        src/coincidencedialog.cpp
        src/coincidencedialog.h
        src/version.h
        ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

# Regenerated on every build, but version.cpp is only rewritten (and recompiled) when the git state changes
add_custom_target(jara_version
        COMMAND ${CMAKE_COMMAND}
                -DVERSION_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/version.cpp.in
                -DVERSION_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/version.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
        COMMENT "Checking git state for version information"
        VERBATIM
)
add_dependencies(jara jara_version)

# Application icon, made from icons/jara.svg by icons/make_icons.sh. The PNGs are the window icon on Linux.
target_sources(jara PRIVATE icons/icons.qrc)
if(APPLE)
    target_sources(jara PRIVATE icons/jara.icns)
    set_source_files_properties(icons/jara.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    # Finder and the Dock cache the icon until the bundle itself changes
    add_custom_command(TARGET jara POST_BUILD COMMAND touch $<TARGET_BUNDLE_DIR:jara> VERBATIM)
elseif(WIN32)
    target_sources(jara PRIVATE icons/jara.rc)
endif()

target_compile_definitions(jara PRIVATE JARA_VERSION="${PROJECT_VERSION}")
target_include_directories(jara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(jara PRIVATE Qt6::Widgets GSL::gsl jibal ZLIB::ZLIB)

# macOS refuses to show native file dialogs for an app bundle without an identifier
set_target_properties(jara PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER fi.jyu.jara
        MACOSX_BUNDLE_BUNDLE_NAME JaRA
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE jara.icns
        WIN32_EXECUTABLE ON
)

# Installing. On Windows the program, the DLLs it needs and Qt (with windeployqt) are installed in one directory, from
# which CPack makes the zip and the MSI. On Linux the installed tree is made into an AppImage with linuxdeploy. On
# macOS the disk image is made by release_scripts/deploy_mac.sh, but the bundle can be installed too.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
endif()
include(GNUInstallDirs)
# An installed program finds JIBAL and GSL where it was linked with them (linuxdeploy and macdeployqt change this)
set_target_properties(jara PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS jara
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
)
if(WIN32)
    qt_generate_deploy_app_script(TARGET jara OUTPUT_SCRIPT jara_deploy_script NO_TRANSLATIONS)
    # Qt's deploy script takes its binary directory from when Qt was found (bin), so it is given here
    install(CODE "set(QT_DEPLOY_BIN_DIR \"${CMAKE_INSTALL_BINDIR}\")")
    install(SCRIPT ${jara_deploy_script})
    # GSL, zlib and JIBAL. Qt is deployed above, system DLLs are not copied. JARA_RUNTIME_DIRS has the directories of
    # the DLLs, by default next to the GSL headers (as in vcpkg) and the JIBAL library.
    get_filename_component(jara_gsl_bin "${GSL_INCLUDE_DIRS}/../bin" ABSOLUTE)
    set(JARA_RUNTIME_DIRS "${jara_gsl_bin}" CACHE STRING "Directories of the DLLs of GSL and zlib")
    install(CODE "set(jara_runtime_dirs \"$<TARGET_FILE_DIR:jibal>\" \"${JARA_RUNTIME_DIRS}\")")
    install(CODE [[
        file(GET_RUNTIME_DEPENDENCIES
                EXECUTABLES "$<TARGET_FILE:jara>"
                RESOLVED_DEPENDENCIES_VAR jara_dlls
                UNRESOLVED_DEPENDENCIES_VAR jara_unresolved
                DIRECTORIES ${jara_runtime_dirs}
                PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^[Qq]t6"
                POST_EXCLUDE_REGEXES "[Ss]ystem32")
        foreach(dll IN LISTS jara_dlls)
            file(INSTALL "${dll}" DESTINATION "${CMAKE_INSTALL_PREFIX}")
        endforeach()
        if(jara_unresolved)
            message(FATAL_ERROR "DLLs not found: ${jara_unresolved}")
        endif()
    ]])
    # The Visual C++ runtime next to the program, so that it runs without the redistributable installed
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    include(InstallRequiredSystemLibraries)
    set_property(INSTALL "$<TARGET_FILE_NAME:jara>" PROPERTY CPACK_START_MENU_SHORTCUTS "JaRA")
elseif(NOT APPLE)
    install(FILES icons/jara.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icons/png/jara_256.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME jara.png)
endif()

# JIBAL configuration and data files (masses, abundances, stopping) installed with the program, so that it runs
# without JIBAL. JaRA looks for jibal.conf in a place relative to the program (src/elements.cpp). The stopping files
# are downloaded once and checked against a known SHA-256, masses and abundances come from the JIBAL installation.
option(JARA_BUNDLE_JIBAL_DATA "Install JIBAL configuration and data files with JaRA" OFF)
if(JARA_BUNDLE_JIBAL_DATA)
    set(jara_jibal_data_url "https://users.jyu.fi/~jaakjuli/jibal/data/data.tar.gz")
    set(jara_jibal_data_sha256 "799a74a37cfd3d003a8b4914e6ff2c554c1b78eacbba84e3a6a414f982ce0bce")
    find_file(JARA_JIBAL_MASSES_FILE masses.dat
            HINTS "${Jibal_DIR}/../../../share/jibal" "${Jibal_DIR}/../../../../share/jibal"
            PATH_SUFFIXES share/jibal
            DOC "masses.dat of JIBAL, abundances.dat is taken from the same directory")
    if(NOT JARA_JIBAL_MASSES_FILE)
        message(FATAL_ERROR "masses.dat of JIBAL not found, set JARA_JIBAL_MASSES_FILE")
    endif()
    get_filename_component(jara_jibal_datadir "${JARA_JIBAL_MASSES_FILE}" DIRECTORY)

    set(jara_jibal_data_archive "${CMAKE_BINARY_DIR}/jibal_data.tar.gz")
    if(NOT EXISTS "${jara_jibal_data_archive}")
        message(STATUS "Downloading JIBAL data files")
        file(DOWNLOAD "${jara_jibal_data_url}" "${jara_jibal_data_archive}.part"
                EXPECTED_HASH SHA256=${jara_jibal_data_sha256} TLS_VERIFY ON STATUS jara_download_status)
        list(GET jara_download_status 0 jara_download_error)
        if(jara_download_error)
            file(REMOVE "${jara_jibal_data_archive}.part")
            message(FATAL_ERROR "Downloading ${jara_jibal_data_url} failed: ${jara_download_status}")
        endif()
        file(RENAME "${jara_jibal_data_archive}.part" "${jara_jibal_data_archive}")
    endif()
    file(SHA256 "${jara_jibal_data_archive}" jara_archive_sha256)
    if(NOT jara_archive_sha256 STREQUAL jara_jibal_data_sha256)
        message(FATAL_ERROR "${jara_jibal_data_archive} does not pass the SHA-256 check")
    endif()
    set(jara_jibal_data_dir "${CMAKE_BINARY_DIR}/jibal_data")
    file(REMOVE_RECURSE "${jara_jibal_data_dir}")
    file(ARCHIVE_EXTRACT INPUT "${jara_jibal_data_archive}" DESTINATION "${jara_jibal_data_dir}")
    file(GLOB jara_jibal_stopping_files "${jara_jibal_data_dir}/*.ele" "${jara_jibal_data_dir}/*.stg")

    # Paths in jibal.conf are relative to the directory it is in
    file(WRITE "${CMAKE_BINARY_DIR}/jibal.conf"
            "datadir = jibal_data\n"
            "masses_file = jibal_data/masses.dat\n"
            "abundances_file = jibal_data/abundances.dat\n"
            "files_file = jibal_data/files.txt\n"
            "assignments_file = jibal_data/assignments.txt\n")
    if(WIN32)
        set(jara_jibal_conf_dir ".")
    elseif(APPLE)
        set(jara_jibal_conf_dir "jara.app/Contents/Resources")
    else()
        set(jara_jibal_conf_dir "share/jara")
    endif()
    install(FILES "${CMAKE_BINARY_DIR}/jibal.conf" DESTINATION ${jara_jibal_conf_dir})
    install(FILES "${jara_jibal_datadir}/masses.dat" "${jara_jibal_datadir}/abundances.dat"
            ${jara_jibal_stopping_files}
            "${jara_jibal_data_dir}/files.txt" "${jara_jibal_data_dir}/assignments.txt"
            DESTINATION ${jara_jibal_conf_dir}/jibal_data)
endif()
if(WIN32)
    set(jara_doc_dir ".")
elseif(APPLE)
    set(jara_doc_dir "jara.app/Contents/Resources")
else()
    set(jara_doc_dir "${CMAKE_INSTALL_DOCDIR}")
endif()
install(FILES LICENSE.txt README.md DESTINATION ${jara_doc_dir})

# Packages, used on Windows: cpack -G "ZIP;WIX" in the build directory. WiX 4 or newer (wix .NET tool) with the UI
# and Util extensions is needed for the MSI.
set(CPACK_PACKAGE_NAME "JaRA")
set(CPACK_PACKAGE_VENDOR "University of Jyväskylä")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Analysis of ToF-ERDA data")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "JaRA")
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "JaRA ${PROJECT_VERSION} Windows x64")
else()
    set(CPACK_PACKAGE_FILE_NAME "JaRA ${PROJECT_VERSION} ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_GENERATOR "ZIP")
set(CPACK_WIX_VERSION 4)
# The same for every version, so that an MSI of a new version replaces the installed one
set(CPACK_WIX_UPGRADE_GUID "6221FD9D-3553-498E-8E71-07FD7CE18A3B")
set(CPACK_WIX_INSTALL_SCOPE "perMachine")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icons/jara.ico")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "JaRA")
include(CPack)
