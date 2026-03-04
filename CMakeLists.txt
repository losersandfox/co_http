cmake_minimum_required(VERSION 3.16)

project("chat_server")
set(CMAKE_CXX_STANDARD 20)
include_directories(include)
find_package(fmt 12 REQUIRED CONFIG)
find_package(spdlog REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)



add_executable(chat_exe src/main.cpp src/server.cpp)
add_executable(chat_test src/test.cpp)
target_link_libraries(chat_exe PRIVATE fmt::fmt)
target_link_libraries(chat_exe PRIVATE spdlog::spdlog)
target_link_libraries(chat_test PRIVATE fmt::fmt)
target_link_libraries(chat_test PRIVATE spdlog::spdlog)
target_include_directories(chat_exe PRIVATE ${LIBURING_INCLUDE_DIRS})
target_link_libraries(chat_exe PRIVATE ${LIBURING_LIBRARIES})
target_compile_options(chat_exe PRIVATE ${LIBURING_CFLAGS_OTHER})

target_include_directories(chat_test PRIVATE ${LIBURING_INCLUDE_DIRS})
target_link_libraries(chat_test PRIVATE ${LIBURING_LIBRARIES})
target_compile_options(chat_test PRIVATE ${LIBURING_CFLAGS_OTHER})
