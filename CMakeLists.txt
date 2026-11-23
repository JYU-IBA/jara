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
        src/stopping.cpp
        src/stopping.h
        src/materialeditor.cpp
        src/materialeditor.h
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
