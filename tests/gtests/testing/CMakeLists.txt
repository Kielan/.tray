add_definitions(${GFLAGS_DEFINES})
add_definitions(${GLOG_DEFINES})
add_definitions(-DTRAY_GFLAGS_NAMESPACE=${GFLAGS_NAMESPACE})

set(INC
  .
  ..
  ${GLOG_INCLUDE_DIRS}
  ${GFLAGS_INCLUDE_DIRS}
  ../../../extern/gtest/include
  ../../../intern/guardedalloc
)

set(INC_SYS)

set(SRC
  testing_main.cc
  testing.h
)

set(LIB
  trayfile_intern_guardedalloc
)

tray_add_lib(trayfile_testing_main
"${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
