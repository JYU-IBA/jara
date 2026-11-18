cmake_minimum_required(VERSION 4.4)
project(jara VERSION 0.1.0 LANGUAGES C CXX)

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
)

target_compile_definitions(jara PRIVATE JARA_VERSION="${PROJECT_VERSION}")
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
