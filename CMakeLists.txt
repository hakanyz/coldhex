cmake_minimum_required(VERSION 3.16)

project(probedesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(SOURCES
    src/app/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.ui
    src/ui/HexMemoryModel.h
    src/ui/HexMemoryModel.cpp
    src/core/FirmwareTarget.h
    src/core/HexParser.h
    src/core/HexParser.cpp
    src/core/FlashCoordinator.h
    src/core/FlashCoordinator.cpp
    src/engine/IProgrammerEngine.h
    src/engine/ProbeRsEngine.h
    src/engine/ProbeRsEngine.cpp
)

qt_add_executable(probedesk
    MANUAL_FINALIZATION
    ${SOURCES}
)

target_include_directories(probedesk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(probedesk PRIVATE
    Qt6::Widgets
)

set_target_properties(probedesk PROPERTIES
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS probedesk
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(probedesk)
