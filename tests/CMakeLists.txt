find_package(GTest REQUIRED)

add_executable(wtng_tests
    test_core.cpp
    test_telnet.cpp
    test_screen.cpp
    test_serial.cpp
    test_ssh_login.cpp
    bench_parser.cpp)
target_link_libraries(wtng_tests PRIVATE wtterminal wtbackends ssh GTest::gtest GTest::gtest_main)
target_include_directories(wtng_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

include(GoogleTest)
gtest_discover_tests(wtng_tests DISCOVERY_MODE PRE_TEST)

# Session store (app code without the GUI)
add_executable(wtng_session_tests
    test_sessions.cpp
    ${PROJECT_SOURCE_DIR}/src/app/SessionStore.cpp
    ${PROJECT_SOURCE_DIR}/src/app/SessionStore.h)
target_include_directories(wtng_session_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wtng_session_tests PRIVATE wtcore Qt6::Gui GTest::gtest GTest::gtest_main)
if(WIN32)
    target_link_libraries(wtng_session_tests PRIVATE crypt32)
endif()
gtest_discover_tests(wtng_session_tests DISCOVERY_MODE PRE_TEST)
