include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_BINARY_DIR}
  )

set(qt5_test_app_UIS mainwin.ui)
set(qt5_test_app_MOC_HDRS mainwin.hpp)
qt5_wrap_ui(qt5_test_app_UIS_H ${qt5_test_app_UIS})
qt5_wrap_cpp(qt5_test_app_MOC_SRCS ${qt5_test_app_MOC_HDRS})

add_executable(qt5_test_app
  ${qt5_test_app_UIS_H}
  ${qt5_test_app_MOC_SRCS}
  ${qt5_test_app_MOC_HDRS}
  mainwin.cpp
  main.cpp
  )
target_link_libraries(qt5_test_app qtmonkey_agent Qt5::Widgets)
