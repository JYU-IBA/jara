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
)

target_compile_definitions(jara PRIVATE JARA_VERSION="${PROJECT_VERSION}")
target_link_libraries(jara PRIVATE Qt6::Widgets GSL::gsl jibal)

set_target_properties(jara PROPERTIES
        MACOSX_BUNDLE ON
        WIN32_EXECUTABLE ON
)
