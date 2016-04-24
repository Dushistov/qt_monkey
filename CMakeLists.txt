#tested with such version, if works with previous also, inform me
cmake_minimum_required(VERSION 3.5)

include(compiler.cmake)
#project(
set(USE_TESTS False CACHE BOOL "enable testing")
set(QT_VARIANT "qt4" CACHE STRING "variant of qt: qt4 or qt5")

if ((NOT ("${QT_VARIANT}" STREQUAL "qt4")) AND
    (NOT ("${QT_VARIANT}" STREQUAL "qt5")))
    message(FATAL_ERROR "only qt4 and qt5 supported")
endif ()

if ("${QT_VARIANT}" STREQUAL "qt4")
  set(QT_USE_QTSCRIPT True)
  set(QT_USE_QTNETWORK True)
  set(QT_USE_QTTEST True)
  find_package(Qt4 REQUIRED)
  include(${QT_USE_FILE})
  include_directories(${QT_INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
  add_definitions(${QT_DEFINITIONS})
endif ()

set(qtmonkey_agent_MOC_HDRS
  user_events_analyzer.hpp
  agent.hpp
  )
qt4_wrap_cpp(qtmonkey_agent_MOC_SRCS ${qtmonkey_agent_MOC_HDRS})

add_library(qtmonkey_agent STATIC
  ${qtmonkey_agent_MOC_SRCS}
  ${qtmonkey_agent_MOC_HDRS}
  agent.cpp
  user_events_analyzer.cpp
  custom_event_analyzer.hpp
  )

add_executable(qtmonkey_app qtmonkey_app.cpp)
target_link_libraries(qtmonkey_app ${QT_LIBRARIES})

if (USE_TESTS)
  enable_testing()
  if ("${QT_VARIANT}" STREQUAL "qt4")
    add_subdirectory(tests/qt4)
  elseif ("${QT_VARIANT}" STREQUAL "qt5")
    add_subdirectory(tests/qt5)
  endif ()
endif ()
