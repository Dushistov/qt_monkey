include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_BINARY_DIR}
  )


set(test_app_UIS mainwin.ui)
set(test_app_MOC_HDRS mainwin.hpp)
if ("${QT_VARIANT}" STREQUAL "qt4")
  qt4_wrap_ui(test_app_UIS_H ${test_app_UIS})
  qt4_wrap_cpp(test_app_MOC_SRCS ${test_app_MOC_HDRS})
elseif ("${QT_VARIANT}" STREQUAL "qt5")
  qt5_wrap_ui(test_app_UIS_H ${test_app_UIS})
  qt5_wrap_cpp(test_app_MOC_SRCS ${test_app_MOC_HDRS})
endif ()

add_executable(test_app
  ${test_app_UIS_H}
  ${test_app_MOC_SRCS}
  ${test_app_MOC_HDRS}
  mainwin.cpp
  main.cpp
  )
target_link_libraries(test_app qtmonkey_agent ${QT_LIBRARIES})
