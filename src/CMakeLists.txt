# --- core: containers, utilities, crypto, telnet protocol (no GUI) ---------
add_library(fscore STATIC
    core/CircularBuffer.h
    core/ScopeGuard.h
    core/SpinMutex.h
    core/Backend.h
    core/Cryptographic.h core/Cryptographic.cpp
    core/TelnetProtocol.h core/TelnetProtocol.cpp)
target_include_directories(fscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fscore PUBLIC fastshell_options Qt6::Core PRIVATE OpenSSL::Crypto)

# --- terminal: VT parser + screen model (no GUI) ---------------------------
add_library(fsterminal STATIC
    terminal/CharWidth.h terminal/CharWidth.cpp
    terminal/VtParser.h terminal/VtParser.cpp
    terminal/Screen.h terminal/Screen.cpp)
target_link_libraries(fsterminal PUBLIC fscore)

# --- backends: local pty, ssh, telnet --------------------------------------
add_library(fsbackends STATIC
    pty/Pty.h pty/Pty.cpp
    pty/ConPty.h pty/ConPty.cpp
    pty/UnixPty.h pty/UnixPty.cpp
    net/SshCommon.h net/SshCommon.cpp
    net/SshBackend.h net/SshBackend.cpp
    net/SftpClient.h net/SftpClient.cpp
    net/TelnetBackend.h net/TelnetBackend.cpp
    net/SerialBackend.h net/SerialBackend.cpp)
target_link_libraries(fsbackends PUBLIC fscore Qt6::Network Qt6::SerialPort PRIVATE ssh)

# --- widgets ---------------------------------------------------------------
add_library(fswidgets STATIC
    widgets/ScrollBar.h widgets/ScrollBar.cpp
    widgets/Splitter.h widgets/Splitter.cpp
    widgets/PopupWidget.h widgets/PopupWidget.cpp)
target_link_libraries(fswidgets PUBLIC fastshell_options Qt6::Widgets)
target_include_directories(fswidgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- application -----------------------------------------------------------
qt_add_executable(fastshell WIN32 MACOSX_BUNDLE
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
    app/UpdateCheck.h app/UpdateCheck.cpp
    app/Updater.h app/Updater.cpp
    app/Icons.h
    app/Logo.h
    app/Theme.h)
if(WIN32)
    # dwmapi: dark title bar and Windows 11 backdrop (Style.cpp).
    target_link_libraries(fastshell PRIVATE crypt32 dwmapi)
    # Remote desktop inside the tabs needs Qt ActiveQt (MSYS2 package
    # mingw-w64-ucrt-x86_64-qt6-activeqt). Without it, RDP sessions open in mstsc.
    find_package(Qt6 QUIET COMPONENTS AxContainer)
    if(TARGET Qt6::AxContainer)
        target_link_libraries(fastshell PRIVATE Qt6::AxContainer)
        target_compile_definitions(fastshell PRIVATE FASTSHELL_HAVE_AXRDP)
        message(STATUS "Remote desktop: embedded (Qt ActiveQt found)")
    else()
        message(STATUS "Remote desktop: external mstsc only (install qt6-activeqt to embed it)")
    endif()
endif()
target_link_libraries(fastshell PRIVATE fsterminal fsbackends fswidgets)
set_target_properties(fastshell PROPERTIES OUTPUT_NAME "FastShell")
if(WIN32)
    # Icon + version information (shown by Windows and used by the installer).
    configure_file(${PROJECT_SOURCE_DIR}/assets/fastshell.rc.in ${CMAKE_CURRENT_BINARY_DIR}/fastshell.rc @ONLY)
    target_sources(fastshell PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/fastshell.rc)
endif()

target_compile_definitions(fastshell PRIVATE FASTSHELL_VERSION="${PROJECT_VERSION}")
