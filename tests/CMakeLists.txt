find_package(GTest REQUIRED)

add_executable(fastshell_tests
    test_core.cpp
    test_telnet.cpp
    test_screen.cpp
    test_serial.cpp
    test_ssh_login.cpp
    test_security.cpp
    bench_parser.cpp)
target_link_libraries(fastshell_tests PRIVATE fsterminal fsbackends ssh GTest::gtest GTest::gtest_main)
target_include_directories(fastshell_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

include(GoogleTest)
gtest_discover_tests(fastshell_tests DISCOVERY_MODE PRE_TEST)

# Session store (app code without the GUI)
add_executable(fastshell_session_tests
    test_sessions.cpp
    test_update.cpp
    test_app_security.cpp
    ${PROJECT_SOURCE_DIR}/src/app/SessionStore.cpp
    ${PROJECT_SOURCE_DIR}/src/app/SessionStore.h
    ${PROJECT_SOURCE_DIR}/src/app/UpdateCheck.cpp
    ${PROJECT_SOURCE_DIR}/src/app/UpdateCheck.h)
target_include_directories(fastshell_session_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(fastshell_session_tests PRIVATE fscore Qt6::Gui GTest::gtest GTest::gtest_main)
if(WIN32)
    target_link_libraries(fastshell_session_tests PRIVATE crypt32)
endif()
gtest_discover_tests(fastshell_session_tests DISCOVERY_MODE PRE_TEST)
