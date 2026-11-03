# --- core: containers, utilities, crypto, telnet protocol (no GUI) ---------
add_library(wtcore STATIC
    core/CircularBuffer.h
    core/ScopeGuard.h
    core/SpinMutex.h
    core/Backend.h
    core/Cryptographic.h core/Cryptographic.cpp
    core/TelnetProtocol.h core/TelnetProtocol.cpp)
target_include_directories(wtcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wtcore PUBLIC wtng_options Qt6::Core PRIVATE OpenSSL::Crypto)

# --- terminal: VT parser + screen model (no GUI) ---------------------------
add_library(wtterminal STATIC
    terminal/CharWidth.h terminal/CharWidth.cpp
    terminal/VtParser.h terminal/VtParser.cpp
    terminal/Screen.h terminal/Screen.cpp)
target_link_libraries(wtterminal PUBLIC wtcore)

# --- backends: local pty, ssh, telnet --------------------------------------
add_library(wtbackends STATIC
    pty/Pty.h pty/Pty.cpp
    pty/ConPty.h pty/ConPty.cpp
    pty/UnixPty.h pty/UnixPty.cpp
    net/SshCommon.h net/SshCommon.cpp
    net/SshBackend.h net/SshBackend.cpp
    net/SftpClient.h net/SftpClient.cpp
    net/TelnetBackend.h net/TelnetBackend.cpp
    net/SerialBackend.h net/SerialBackend.cpp)
target_link_libraries(wtbackends PUBLIC wtcore Qt6::Network Qt6::SerialPort PRIVATE ssh)

# --- widgets ---------------------------------------------------------------
add_library(wtwidgets STATIC
    widgets/ScrollBar.h widgets/ScrollBar.cpp
    widgets/Splitter.h widgets/Splitter.cpp
    widgets/PopupWidget.h widgets/PopupWidget.cpp)
target_link_libraries(wtwidgets PUBLIC wtng_options Qt6::Widgets)
target_include_directories(wtwidgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- application -----------------------------------------------------------
qt_add_executable(windterm-ng WIN32 MACOSX_BUNDLE
    app/main.cpp
    app/MainWindow.h app/MainWindow.cpp
    app/TerminalView.h app/TerminalView.cpp
    app/KeyMapper.h app/KeyMapper.cpp
    app/SessionStore.h app/SessionStore.cpp
    app/SessionDialog.h app/SessionDialog.cpp
    app/SessionPanel.h app/SessionPanel.cpp
    app/FilerPanel.h app/FilerPanel.cpp
    app/Panels.h app/Panels.cpp
    app/ManageDialogs.h app/ManageDialogs.cpp
    app/TabPage.h app/TabPage.cpp
    app/DropPreview.h app/DropPreview.cpp
    app/RdpView.h app/RdpView.cpp
    app/Style.h app/Style.cpp
    app/Icons.h
    app/Logo.h
    app/Theme.h)
if(WIN32)
    # dwmapi: dark title bar and Windows 11 backdrop (Style.cpp).
    target_link_libraries(windterm-ng PRIVATE crypt32 dwmapi)
    # Remote desktop inside the tabs needs Qt ActiveQt (MSYS2 package
    # mingw-w64-ucrt-x86_64-qt6-activeqt). Without it, RDP sessions open in mstsc.
    find_package(Qt6 QUIET COMPONENTS AxContainer)
    if(TARGET Qt6::AxContainer)
        target_link_libraries(windterm-ng PRIVATE Qt6::AxContainer)
        target_compile_definitions(windterm-ng PRIVATE WTNG_HAVE_AXRDP)
        message(STATUS "Remote desktop: embedded (Qt ActiveQt found)")
    else()
        message(STATUS "Remote desktop: external mstsc only (install qt6-activeqt to embed it)")
    endif()
endif()
target_link_libraries(windterm-ng PRIVATE wtterminal wtbackends wtwidgets)
set_target_properties(windterm-ng PROPERTIES OUTPUT_NAME "FastShell")
if(WIN32)
    target_sources(windterm-ng PRIVATE ${PROJECT_SOURCE_DIR}/assets/emfhexa.rc)
endif()

target_compile_definitions(windterm-ng PRIVATE WTNG_VERSION="${PROJECT_VERSION}")
