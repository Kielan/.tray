# Always run tests from install path, so all required scripts and libraries
# are available and we are testing the actual installation layout.
#
# Getting the install path of the executable is somewhat involved, as there are
# no direct CMake generator expressions to get the install paths of executables.
get_property(GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(GENERATOR_IS_MULTI_CONFIG)
  string(REPLACE "\${BUILD_TYPE}" "$<CONFIG>" TEST_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
else()
  string(REPLACE "\${BUILD_TYPE}" "" TEST_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
endif()

# Path to Tray ane ex for all platforms.
if(MSVC)
  set(TEST_TRAY_EXE ${TEST_INSTALL_DIR}/tray.exe)
  set(_default_test_python_exe "${TEST_INSTALL_DIR}/${TRAY_VERSION_MAJOR}.$_VERSION_MINOR}/python/bin/python$<$<CONFIG:Debug>:_d>")
elseif(APPLE)
  set(TEST_TRAY_EXE ${TEST_INSTALL_DIR}/Tray.app/Contents/MacOS/Tray)
else()
  if(WITH_INSTALL_PORTABLE)
    set(TEST_TRAY_EXE ${TEST_INSTALL_DIR}/tray)
  else()
    set(TEST_TRAY_EXE ${TEST_INSTALL_DIR}/bin/tray)
  endif()
endif()

# For testing with Valgrind
# set(TEST_TRAY_EXE valgrind --track-origins=yes --error-limit=no ${TEST_TRAY_EXE})

# Standard Tray arguments for running tests.
# Specify exit code by adding 1 at the end.
set(TEST_TRAY_EXE_PARAMS --background -noaudio --factory-startup --debug-memory --debug-exit-on-error)

# GTest
add_subdirectory(gtests)
