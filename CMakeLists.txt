cmake_minimum_required(VERSION 3.19)
project(razer-rebind VERSION 0.4.0 LANGUAGES CXX)

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
target_compile_definitions(razer-rebind PRIVATE RAZER_REBIND_VERSION="${PROJECT_VERSION}")
target_link_libraries(razer-rebind PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus PkgConfig::LIBEVDEV Threads::Threads)

include(GNUInstallDirs)
configure_file(data/razer-rebind.service.in razer-rebind.service @ONLY)
install(TARGETS razer-rebind RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/razer-rebind.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/razer-rebind.service DESTINATION lib/systemd/user)
install(FILES data/70-razer-rebind.rules DESTINATION lib/udev/rules.d)
