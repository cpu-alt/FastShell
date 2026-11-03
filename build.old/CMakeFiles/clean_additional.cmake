# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  [[src\CMakeFiles\windterm-ng_autogen.dir\AutogenUsed.txt]]
  [[src\CMakeFiles\windterm-ng_autogen.dir\ParseCache.txt]]
  [[src\CMakeFiles\wtbackends_autogen.dir\AutogenUsed.txt]]
  [[src\CMakeFiles\wtbackends_autogen.dir\ParseCache.txt]]
  [[src\CMakeFiles\wtcore_autogen.dir\AutogenUsed.txt]]
  [[src\CMakeFiles\wtcore_autogen.dir\ParseCache.txt]]
  [[src\CMakeFiles\wtterminal_autogen.dir\AutogenUsed.txt]]
  [[src\CMakeFiles\wtterminal_autogen.dir\ParseCache.txt]]
  [[src\CMakeFiles\wtwidgets_autogen.dir\AutogenUsed.txt]]
  [[src\CMakeFiles\wtwidgets_autogen.dir\ParseCache.txt]]
  [[src\windterm-ng_autogen]]
  [[src\wtbackends_autogen]]
  [[src\wtcore_autogen]]
  [[src\wtterminal_autogen]]
  [[src\wtwidgets_autogen]]
  [[tests\CMakeFiles\wtng_session_tests_autogen.dir\AutogenUsed.txt]]
  [[tests\CMakeFiles\wtng_session_tests_autogen.dir\ParseCache.txt]]
  [[tests\CMakeFiles\wtng_tests_autogen.dir\AutogenUsed.txt]]
  [[tests\CMakeFiles\wtng_tests_autogen.dir\ParseCache.txt]]
  [[tests\wtng_session_tests_autogen]]
  [[tests\wtng_tests_autogen]]
  )
endif()
