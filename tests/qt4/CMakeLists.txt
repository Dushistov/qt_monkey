include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(qt4_test_app main.cpp)
target_link_libraries(qt4_test_app qtmonkey_agent ${QT_LIBRARIES})
