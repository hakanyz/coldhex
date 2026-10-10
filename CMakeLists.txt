cmake_minimum_required(VERSION 3.16)

project(coldhex VERSION 0.1.0 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

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
    src/engine/ProbeRsLocator.h
    src/engine/ProbeRsLocator.cpp
    src/engine/UsbScanner.h
    src/engine/UsbScanner.cpp
    src/engine/SystemCheck.h
    src/engine/SystemCheck.cpp
    src/ui/SystemCheckDialog.h
    src/ui/ConsoleDock.h
    src/ui/ConsoleDock.cpp
    src/ui/ChipSelector.h
    src/ui/ChipSelector.cpp
    src/ui/SystemCheckDialog.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/engine/IProgrammerEngine.h
    src/engine/ProbeRsEngine.h
    src/engine/ProbeRsEngine.cpp
    src/engine/ConnectionAdvice.h
    src/engine/ConnectionAdvice.cpp
)

qt_add_executable(coldhex
    MANUAL_FINALIZATION
    ${SOURCES}
)

# Application icon: embedded in the Windows executable and available to the program as a Qt resource.
file(GLOB TOOLBAR_ICONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/toolbar/*.svg)
file(GLOB DOCK_ICONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/dock/*.svg)
qt_add_resources(coldhex "app_icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/coldhex.png" ${TOOLBAR_ICONS} ${DOCK_ICONS}
)
qt_add_resources(coldhex "theme"
    PREFIX "/theme"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/theme"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/theme/dark.qss"
)
if(WIN32)
    set(COLDHEX_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/coldhex.ico")
    configure_file(resources/coldhex.rc.in ${CMAKE_CURRENT_BINARY_DIR}/coldhex.rc @ONLY)
    target_sources(coldhex PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/coldhex.rc)
endif()
target_compile_definitions(coldhex PRIVATE COLDHEX_VERSION="${PROJECT_VERSION}")

# Chip descriptions in resources/chips are built into the executable (see resources/chips/README.md).
file(GLOB CHIP_YAMLS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/chips/*.yaml)
if(CHIP_YAMLS)
    qt_add_resources(coldhex "bundled_chips"
        PREFIX "/chips"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/chips"
        FILES ${CHIP_YAMLS}
    )
endif()

target_include_directories(coldhex PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(coldhex PRIVATE
    Qt6::Widgets
)

set_target_properties(coldhex PROPERTIES
    WIN32_EXECUTABLE TRUE
)

qt_finalize_executable(coldhex)

# ---------------------------------------------------------------------------
# Installation and packaging
# ---------------------------------------------------------------------------
# A probe-rs executable can be shipped inside the package. Point this at a folder that
# contains it (probe-rs or probe-rs.exe); the packaging workflow downloads it for you.
set(COLDHEX_PROBE_RS_DIR "" CACHE PATH "Folder containing the probe-rs executable to ship with the package")

include(GNUInstallDirs)
if(WIN32)
    # Qt's deploy script puts the Qt DLLs in bin/, so the program lives there too.
    install(TARGETS coldhex RUNTIME DESTINATION bin)
    # Copies the Qt DLLs and plugins next to the program when the package is built.
    if(COMMAND qt_generate_deploy_app_script)
        qt_generate_deploy_app_script(TARGET coldhex OUTPUT_SCRIPT COLDHEX_DEPLOY_SCRIPT NO_UNSUPPORTED_PLATFORM_ERROR)
        install(SCRIPT ${COLDHEX_DEPLOY_SCRIPT})
    endif()
    set(COLDHEX_PROBE_RS_DEST "bin")
    set(COLDHEX_NOTICE_DEST ".")
else()
    install(TARGETS coldhex RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/coldhex.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icons/coldhex.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES resources/icons/coldhex.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES packaging/linux/69-coldhex.rules DESTINATION lib/udev/rules.d)
    # The program looks for probe-rs in ../lib/coldhex relative to its own folder.
    set(COLDHEX_PROBE_RS_DEST "lib/coldhex")
    set(COLDHEX_NOTICE_DEST "share/doc/coldhex")
endif()
install(FILES
    packaging/THIRD_PARTY_NOTICES.md
    packaging/licenses/Qt-LGPL-3.0.txt
    packaging/licenses/GPL-3.0.txt
    DESTINATION ${COLDHEX_NOTICE_DEST})

if(COLDHEX_PROBE_RS_DIR)
    find_program(COLDHEX_PROBE_RS_EXE NAMES probe-rs PATHS "${COLDHEX_PROBE_RS_DIR}" NO_DEFAULT_PATH REQUIRED)
    install(PROGRAMS "${COLDHEX_PROBE_RS_EXE}" DESTINATION ${COLDHEX_PROBE_RS_DEST})
    foreach(license LICENSE-MIT LICENSE-APACHE)
        if(EXISTS "${COLDHEX_PROBE_RS_DIR}/${license}")
            install(FILES "${COLDHEX_PROBE_RS_DIR}/${license}" DESTINATION ${COLDHEX_NOTICE_DEST}
                    RENAME "probe-rs-${license}")
        endif()
    endforeach()
endif()

set(CPACK_PACKAGE_NAME "coldhex")
set(CPACK_PACKAGE_VENDOR "ColdHex Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Flash and manage ARM Cortex-M microcontrollers")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_EXECUTABLES "coldhex;ColdHex")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ColdHex")

if(WIN32)
    # .zip (portable) and .exe (installer)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "ColdHex")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "coldhex.exe")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
else()
    # .deb and .tar.gz
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ColdHex Project")
    set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libudev1, libqt6svg6")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postrm")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
endif()
include(CPack)


option(COLDHEX_BUILD_TESTS "Build unit tests" ON)
if(COLDHEX_BUILD_TESTS)
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
        src/engine/ConnectionAdvice.cpp
        src/engine/ChipDescription.cpp
        src/engine/ProbeRsLocator.cpp
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
        src/engine/ConnectionAdvice.cpp
        src/engine/ChipDescription.cpp
        src/engine/ProbeRsLocator.cpp
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

    add_executable(tst_systemcheck
        tests/tst_systemcheck.cpp
        src/engine/SystemCheck.cpp
        src/engine/SystemCheck.h
        src/engine/UsbScanner.cpp
        src/engine/UsbScanner.h
        src/engine/ProbeRsEngine.cpp
        src/engine/ConnectionAdvice.cpp
        src/engine/ChipDescription.cpp
        src/engine/ProbeRsEngine.h
        src/engine/ProbeRsLocator.cpp
        src/engine/IProgrammerEngine.h
    )
    target_include_directories(tst_systemcheck PRIVATE src)
    target_link_libraries(tst_systemcheck PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_systemcheck COMMAND tst_systemcheck)

    add_executable(tst_usbscanner
        tests/tst_usbscanner.cpp
        src/engine/UsbScanner.cpp
        src/engine/UsbScanner.h
        src/engine/ProbeRsLocator.cpp
        src/engine/ProbeRsLocator.h
    )
    target_include_directories(tst_usbscanner PRIVATE src)
    target_link_libraries(tst_usbscanner PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_usbscanner COMMAND tst_usbscanner)

    add_executable(tst_connectionadvice
        tests/tst_connectionadvice.cpp
        src/engine/ConnectionAdvice.cpp
        src/engine/ConnectionAdvice.h
    )
    target_include_directories(tst_connectionadvice PRIVATE src)
    target_link_libraries(tst_connectionadvice PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tst_connectionadvice COMMAND tst_connectionadvice)
endif()
