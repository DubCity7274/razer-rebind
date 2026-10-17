cmake_minimum_required(VERSION 3.19)
project(razer-rebind VERSION 0.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network DBus)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)

qt_standard_project_setup()

qt_add_executable(razer-rebind
    src/main.cpp
    src/keys.cpp src/keys.h
    src/razerdevice.cpp src/razerdevice.h
    src/remapper.cpp src/remapper.h
    src/remapmanager.cpp src/remapmanager.h
    src/config.cpp src/config.h
    src/notifications.cpp src/notifications.h
    src/batterymonitor.cpp src/batterymonitor.h
    src/settingsdialog.cpp src/settingsdialog.h
    src/inputdetector.cpp src/inputdetector.h
    src/hotkeydialog.cpp src/hotkeydialog.h
    src/deviceview.cpp src/deviceview.h
    src/bindingdialog.cpp src/bindingdialog.h
    src/mainwindow.cpp src/mainwindow.h
)
target_compile_options(razer-rebind PRIVATE -Wall -Wextra)

# The udev rule is compiled in so `razer-rebind --print-udev-rule` can install
# it on systems where no package did (AppImage, source builds).
file(READ data/70-razer-rebind.rules RAZER_REBIND_UDEV_RULE)
file(CONFIGURE OUTPUT generated/udevrule.h
     CONTENT "#pragma once\nconstexpr const char *UdevRule = R\"rule(@RAZER_REBIND_UDEV_RULE@)rule\";\n" @ONLY)
target_include_directories(razer-rebind PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(razer-rebind PRIVATE RAZER_REBIND_VERSION="${PROJECT_VERSION}")
target_link_libraries(razer-rebind PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus PkgConfig::LIBEVDEV Threads::Threads)

# Tests need no hardware: ctest --test-dir build
include(CTest)
if(BUILD_TESTING)
    add_executable(remapper_test tests/remapper_test.cpp
        src/remapper.cpp src/keys.cpp src/config.cpp src/razerdevice.cpp)
    target_include_directories(remapper_test PRIVATE src)
    target_link_libraries(remapper_test PRIVATE Qt6::Core Qt6::DBus PkgConfig::LIBEVDEV Threads::Threads)
    add_test(NAME remapper COMMAND remapper_test)
endif()

include(GNUInstallDirs)
configure_file(data/razer-rebind.service.in razer-rebind.service @ONLY)
install(TARGETS razer-rebind RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/razer-rebind.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/razer-rebind.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/razer-rebind.service DESTINATION lib/systemd/user)
install(FILES data/70-razer-rebind.rules DESTINATION lib/udev/rules.d)

# Distribution packages build docs into the package themselves; RPM uses %doc.
option(RAZER_REBIND_INSTALL_DOCS "Install README and LICENSE to share/doc" OFF)
if(RAZER_REBIND_INSTALL_DOCS)
    install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

# .deb packages: cmake -DCMAKE_INSTALL_PREFIX=/usr -DRAZER_REBIND_DEB_DISTRO=debian12 ... && cpack
set(RAZER_REBIND_DEB_DISTRO "" CACHE STRING "Distro tag for .deb packages, e.g. debian12 or ubuntu24.04")
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME razer-rebind)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "DubCity7274 <111620773+DubCity7274@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Key and button rebinding for Razer keyboards and mice (OpenRazer)")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"Razer Rebind is a Qt 6 app for rebinding keys and buttons on Razer devices
driven by the OpenRazer kernel drivers, with per-device profiles, global
hotkeys and low battery alerts. It installs a udev rule that gives the user at
the local seat access to Razer input devices and /dev/uinput.")
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/DubCity7274/razer-rebind")
if(RAZER_REBIND_DEB_DISTRO)
    set(CPACK_DEBIAN_PACKAGE_RELEASE "1~${RAZER_REBIND_DEB_DISTRO}")
    execute_process(COMMAND dpkg --print-architecture OUTPUT_VARIABLE deb_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    # GitHub release assets can't contain "~", so the file name spells it out.
    set(CPACK_DEBIAN_FILE_NAME "razer-rebind_${PROJECT_VERSION}-1_${RAZER_REBIND_DEB_DISTRO}_${deb_arch}.deb")
endif()
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Platform plugins aren't linked, so shlibdeps can't see them.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
include(CPack)
