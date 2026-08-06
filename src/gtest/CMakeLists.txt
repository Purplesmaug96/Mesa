# Mesa3D - src/gtest/ (mirror of src/gtest/meson.build)
# SPDX-License-Identifier: MIT

add_library(gtest STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src/gtest-all.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/gtest_main.cc")
target_include_directories(gtest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_options(gtest PRIVATE -w)
set_target_properties(gtest PROPERTIES POSITION_INDEPENDENT_CODE ON EXCLUDE_FROM_ALL ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(gtest PRIVATE -Wno-deprecated)
endif()
target_link_libraries(gtest PRIVATE ${dep_thread})

add_library(idep_gtest INTERFACE)
target_link_libraries(idep_gtest INTERFACE gtest ${dep_thread})
target_include_directories(idep_gtest INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(MESA_LAST_TARGET "idep_gtest" PARENT_SCOPE)
