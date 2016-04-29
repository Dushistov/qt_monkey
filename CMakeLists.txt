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
  include_directories(${QT_INCLUDE_DIR})
  add_definitions(${QT_DEFINITIONS})
elseif ("${QT_VARIANT}" STREQUAL "qt5")
  find_package(Qt5Widgets)
  find_package(Qt5Network)
  find_package(Qt5Test)
  find_package(Qt5Script)
  include_directories(${Qt5Widgets_INCLUDE_DIRS})
  set(QT_LIBRARIES Qt5::Widgets Qt5::Network Qt5::Test Qt5::Script)
endif ()

include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(qtmonkey_agent_MOC_HDRS
  user_events_analyzer.hpp
  agent_qtmonkey_communication.hpp
  agent.hpp
  script_api.hpp
  )

set(qtmonkey_app_MOC_HDRS
  agent_qtmonkey_communication.hpp
  qtmonkey.hpp
  )

set(qtmonkey_gui_MOC_HDRS qtmonkey_gui.hpp)
set(qtmonkey_gui_UIS qtmonkey_gui.ui)

if ("${QT_VARIANT}" STREQUAL "qt4")
  qt4_wrap_cpp(qtmonkey_agent_MOC_SRCS ${qtmonkey_agent_MOC_HDRS})
  qt4_wrap_cpp(qtmonkey_app_MOC_SRCS ${qtmonkey_app_MOC_HDRS})
  qt4_wrap_cpp(qtmonkey_gui_MOC_SRCS ${qtmonkey_gui_MOC_HDRS})
  qt4_wrap_ui(qtmonkey_gui_UIS_H ${qtmonkey_gui_UIS})
elseif ("${QT_VARIANT}" STREQUAL "qt5")
  qt5_wrap_cpp(qtmonkey_agent_MOC_SRCS ${qtmonkey_agent_MOC_HDRS})
  qt5_wrap_cpp(qtmonkey_app_MOC_SRCS ${qtmonkey_app_MOC_HDRS})
  qt5_wrap_cpp(qtmonkey_gui_MOC_SRCS ${qtmonkey_gui_MOC_HDRS})
  qt5_wrap_ui(qtmonkey_gui_UIS_H ${qtmonkey_gui_UIS})
endif ()

add_library(qtmonkey_agent STATIC
  ${qtmonkey_agent_MOC_SRCS}
  ${qtmonkey_agent_MOC_HDRS}
  user_events_analyzer.cpp
  custom_event_analyzer.hpp
  agent_qtmonkey_communication.cpp
  agent.cpp
  script_runner.cpp
  script_runner.hpp
  script.hpp
  script.cpp
  script_api.cpp
  )
target_link_libraries(qtmonkey_agent ${QT_LIBRARIES})

add_library(common_app_lib STATIC
  contrib/json11/json11.cpp
  contrib/json11/json11.hpp
  common.cpp
  common.hpp
  qtmonkey_app_api.hpp
  qtmonkey_app_api.cpp
  )
target_include_directories(common_app_lib PRIVATE contrib/json11)
target_link_libraries(common_app_lib ${QT_LIBRARIES})

add_executable(qtmonkey_app
  ${qtmonkey_app_MOC_SRCS}
  ${qtmonkey_app_MOC_HDRS}
  agent_qtmonkey_communication.cpp
  qtmonkey.cpp
  qtmonkey_app.cpp
  )
target_include_directories(qtmonkey_app PRIVATE contrib/json11)
target_link_libraries(qtmonkey_app ${QT_LIBRARIES} common_app_lib)

add_executable(qtmonkey_gui
  ${qtmonkey_gui_MOC_SRCS}
  ${qtmonkey_gui_MOC_HDRS}
  ${qtmonkey_gui_UIS_H}
  qtmonkey_gui.cpp
  )
target_compile_definitions(qtmonkey_gui PRIVATE -DQTMONKEY_APP_NAME="$<TARGET_FILE_NAME:qtmonkey_app>")
target_include_directories(qtmonkey_gui PRIVATE contrib/json11)
target_link_libraries(qtmonkey_gui ${QT_LIBRARIES} common_app_lib)

if (USE_TESTS)
  enable_testing()
  add_subdirectory(contrib/gtest/googletest)
  include_directories(contrib/gtest/googletest/include)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(run_unit_tests tests/unit_tests.cpp)
  target_include_directories(run_unit_tests PRIVATE contrib/json11)
  target_link_libraries(run_unit_tests qtmonkey_agent gtest gtest_main ${QT_LIBRARIES} common_app_lib)
  add_test(unit_tests run_unit_tests)

  add_executable(json11_test contrib/json11/test.cpp)
  target_link_libraries(json11_test common_app_lib)
  add_test(json11_test json11_test)

  if ("${QT_VARIANT}" STREQUAL "qt4")
    add_subdirectory(tests/qt4)
  elseif ("${QT_VARIANT}" STREQUAL "qt5")
    add_subdirectory(tests/qt5)
  endif ()
endif ()
