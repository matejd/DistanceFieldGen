cmake_minimum_required(VERSION 2.8)
project(DistanceFieldGen)

set(CMAKE_CXX_COMPILER clang) # Force clang.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Weverything -Wno-c++98-compat -Wno-unused-member-function -std=c++11") 

add_executable(DistanceFieldGen main.cpp)
set_target_properties(DistanceFieldGen PROPERTIES OUTPUT_NAME dfgen)
target_link_libraries(DistanceFieldGen m stdc++ assimp CGAL boost_thread boost_system gmp mpfr)

add_executable(DistanceFieldExample example/main.cpp)
set_target_properties(DistanceFieldExample PROPERTIES OUTPUT_NAME example)
target_link_libraries(DistanceFieldExample m stdc++ GL GLEW glfw)
