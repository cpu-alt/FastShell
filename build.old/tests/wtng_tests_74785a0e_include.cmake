if(EXISTS "C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests.exe")
  if(NOT EXISTS "C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests_74785a0e_tests.cmake" OR
     NOT "C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests_74785a0e_tests.cmake" IS_NEWER_THAN "C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests.exe" OR
     NOT "C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests_74785a0e_tests.cmake" IS_NEWER_THAN "${CMAKE_CURRENT_LIST_FILE}")
    include("C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests_74785a0e_discovery.cmake")
  endif()
endif()
if(EXISTS "C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests_74785a0e_tests.cmake")
  include("C:/Users/Patrizio/Desktop/Wind/WindTerm-NG/build/tests/wtng_tests_74785a0e_tests.cmake")
else()
  add_test(wtng_tests_NOT_BUILT wtng_tests_NOT_BUILT)
endif()
