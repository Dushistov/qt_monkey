include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/../..
  ${CMAKE_CURRENT_BINARY_DIR}
  )

set(qt4_test_app_UIS mainwin.ui)
set(qt4_test_app_MOC_HDRS mainwin.hpp)
qt4_wrap_ui(qt4_test_app_UIS_H ${qt4_test_app_UIS})
qt4_wrap_cpp(qt4_test_app_MOC_SRCS ${qt4_test_app_MOC_HDRS})

add_executable(qt4_test_app
  ${qt4_test_app_UIS_H}
  ${qt4_test_app_MOC_SRCS}
  ${qt4_test_app_MOC_HDRS}
  mainwin.cpp
  main.cpp
  )
target_link_libraries(qt4_test_app qtmonkey_agent ${QT_LIBRARIES})
