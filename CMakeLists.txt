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
    src/core/AppSettings.h
    src/core/AppSettings.cpp
    src/core/SlotValidator.h
    src/core/SlotValidator.cpp
    src/core/ChipCatalog.h
    src/core/ChipCatalog.cpp
    src/engine/ChipDescription.h
    src/engine/ChipDescription.cpp
    src/engine/BundledChips.h
    src/engine/BundledChips.cpp
    src/engine/IProgrammerEngine.h
    src/engine/ProbeRsEngine.h
    src/engine/ProbeRsEngine.cpp
)

qt_add_executable(probedesk
    MANUAL_FINALIZATION
    ${SOURCES}
)

# Chip descriptions in resources/chips are built into the executable (see resources/chips/README.md).
file(GLOB CHIP_YAMLS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/chips/*.yaml)
if(CHIP_YAMLS)
    qt_add_resources(probedesk "bundled_chips"
        PREFIX "/chips"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/chips"
        FILES ${CHIP_YAMLS}
    )
endif()

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

option(PROBEDESK_BUILD_TESTS "Build unit tests" ON)
if(PROBEDESK_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(tst_flashcoordinator
        tests/tst_flashcoordinator.cpp
        tests/MockEngine.h
        src/core/FlashCoordinator.cpp
        src/core/FlashCoordinator.h
        src/engine/IProgrammerEngine.h
    )
    target_include_directories(tst_flashcoordinator PRIVATE src tests)
    target_link_libraries(tst_flashcoordinator PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_flashcoordinator COMMAND tst_flashcoordinator)

    add_executable(tst_hexparser
        tests/tst_hexparser.cpp
        src/core/HexParser.cpp
        src/core/HexParser.h
    )
    target_include_directories(tst_hexparser PRIVATE src)
    target_link_libraries(tst_hexparser PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_hexparser COMMAND tst_hexparser)

    add_executable(tst_appsettings
        tests/tst_appsettings.cpp
        src/core/AppSettings.cpp
        src/core/AppSettings.h
    )
    target_include_directories(tst_appsettings PRIVATE src)
    target_link_libraries(tst_appsettings PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_appsettings COMMAND tst_appsettings)

    add_executable(tst_slotvalidator
        tests/tst_slotvalidator.cpp
        src/core/SlotValidator.cpp
        src/core/SlotValidator.h
    )
    target_include_directories(tst_slotvalidator PRIVATE src)
    target_link_libraries(tst_slotvalidator PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_slotvalidator COMMAND tst_slotvalidator)

    add_executable(tst_probelist
        tests/tst_proberslist.cpp
        src/engine/ProbeRsEngine.cpp
        src/engine/ProbeRsEngine.h
        src/engine/IProgrammerEngine.h
    )
    target_include_directories(tst_probelist PRIVATE src)
    target_link_libraries(tst_probelist PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_probelist COMMAND tst_probelist)

    add_executable(tst_chipcatalog
        tests/tst_chipcatalog.cpp
        src/core/ChipCatalog.cpp
        src/core/ChipCatalog.h
        src/engine/ProbeRsEngine.cpp
        src/engine/ProbeRsEngine.h
        src/engine/IProgrammerEngine.h
    )
    target_include_directories(tst_chipcatalog PRIVATE src)
    target_link_libraries(tst_chipcatalog PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_chipcatalog COMMAND tst_chipcatalog)

    add_executable(tst_chipdescription
        tests/tst_chipdescription.cpp
        src/engine/ChipDescription.cpp
        src/engine/ChipDescription.h
    )
    target_include_directories(tst_chipdescription PRIVATE src)
    target_link_libraries(tst_chipdescription PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_chipdescription COMMAND tst_chipdescription)

    add_executable(tst_bundledchips
        tests/tst_bundledchips.cpp
        src/engine/BundledChips.cpp
        src/engine/BundledChips.h
    )
    target_include_directories(tst_bundledchips PRIVATE src)
    target_link_libraries(tst_bundledchips PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_bundledchips COMMAND tst_bundledchips)
endif()
