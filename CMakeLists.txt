cmake_minimum_required(VERSION 3.16)
project(recovery_qt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/recoveryengine.cpp
    src/statemanager.cpp
    src/logmodel.cpp
    src/settingsdialog.cpp
)

set(HEADERS
    src/mainwindow.h
    src/recoveryengine.h
    src/statemanager.h
    src/logmodel.h
    src/settingsdialog.h
)

add_executable(recovery_qt ${SOURCES} ${HEADERS})

target_link_libraries(recovery_qt PRIVATE Qt5::Widgets)

target_include_directories(recovery_qt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

install(TARGETS recovery_qt DESTINATION bin)
