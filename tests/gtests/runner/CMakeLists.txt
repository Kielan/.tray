# Build the test runner. This runner takes care of running all GTests, i.e.
# the code that was built using the tray_add_test_lib() CMake macro (see
# macros.cmake).
set(SRC tray_test.cc)

if(WITH_BUILDINFO)
  list(APPEND SRC
    "$<TARGET_OBJECTS:buildinfoobj>"
  )
endif()
