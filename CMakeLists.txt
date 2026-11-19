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

add_executable(jara
        main.cpp
        mainwindow.cpp
        mainwindow.h
        project.cpp
        project.h
        projectpanel.cpp
        projectpanel.h
        measurementview.cpp
        measurementview.h
        energyspectrum.cpp
        energyspectrum.h
        energyspectrumview.cpp
        energyspectrumview.h
        spectrumplotwidget.cpp
        spectrumplotwidget.h
        settingsview.cpp
        settingsview.h
        tabwidget.cpp
        tabwidget.h
        histogramwidget.cpp
        histogramwidget.h
        tofedata.cpp
        tofedata.h
        elements.cpp
        elements.h
        elementdialog.cpp
        elementdialog.h
        colorbutton.cpp
        colorbutton.h
        defaults.cpp
        defaults.h
        settingseditors.cpp
        settingseditors.h
        preferencesdialog.cpp
        preferencesdialog.h
        units.h
        projectimportdialog.cpp
        projectimportdialog.h
        tofcalibration.cpp
        tofcalibration.h
        tofcalibrationwindow.cpp
        tofcalibrationwindow.h
        calibrationplotwidget.cpp
        calibrationplotwidget.h
        stopping.cpp
        stopping.h
        materialeditor.cpp
        materialeditor.h
        foildialog.cpp
        foildialog.h
        depthprofile.cpp
        depthprofile.h
        depthprofileview.cpp
        depthprofileview.h
        efficiencydialog.cpp
        efficiencydialog.h
        version.h
        ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
)

# Regenerated on every build, but version.cpp is only rewritten (and recompiled) when the git state changes
add_custom_target(jara_version
        COMMAND ${CMAKE_COMMAND}
                -DVERSION_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in
                -DVERSION_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/version.cpp
                -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.cpp
        COMMENT "Checking git state for version information"
        VERBATIM
)
add_dependencies(jara jara_version)

target_compile_definitions(jara PRIVATE JARA_VERSION="${PROJECT_VERSION}")
target_include_directories(jara PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jara PRIVATE Qt6::Widgets GSL::gsl jibal)

# macOS refuses to show native file dialogs for an app bundle without an identifier
set_target_properties(jara PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER fi.jyu.jara
        MACOSX_BUNDLE_BUNDLE_NAME JaRA
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        WIN32_EXECUTABLE ON
)
