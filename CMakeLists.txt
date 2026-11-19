cmake_minimum_required(VERSION 4.4)
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
        src/measurementview.cpp
        src/measurementview.h
        src/energyspectrum.cpp
        src/energyspectrum.h
        src/energyspectrumview.cpp
        src/energyspectrumview.h
        src/spectrumplotwidget.cpp
        src/spectrumplotwidget.h
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
        src/tofcalibration.cpp
        src/tofcalibration.h
        src/tofcalibrationwindow.cpp
        src/tofcalibrationwindow.h
        src/calibrationplotwidget.cpp
        src/calibrationplotwidget.h
        src/stopping.cpp
        src/stopping.h
        src/materialeditor.cpp
        src/materialeditor.h
        src/foildialog.cpp
        src/foildialog.h
        src/depthprofile.cpp
        src/depthprofile.h
        src/depthprofileview.cpp
        src/depthprofileview.h
        src/efficiencydialog.cpp
        src/efficiencydialog.h
        src/archive.cpp
        src/archive.h
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
        WIN32_EXECUTABLE ON
)
