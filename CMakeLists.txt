cmake_minimum_required(VERSION 3.12)
project(AsyncServiceSockets LANGUAGES CXX)
find_package(VTK REQUIRED
COMPONENTS
    CommonSystem
)

if (NOT VTK_FOUND)
  message(FATAL_ERROR "Unable to find VTK.")
endif()

set(RXCPP_DIR "$ENV{RXCPP_DIR}")
message(STATUS "rxcpp: ${RXCPP_DIR}")

# Prevent a "command line is too long" failure in Windows.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE "ON" CACHE BOOL "Force Ninja to use response files.")
add_executable(main MACOSX_BUNDLE main.cxx )
# for rxcpp, c++17 may be needed.
target_compile_features(main PRIVATE cxx_std_17)
target_include_directories(main PRIVATE ${RXCPP_DIR}/Rx/v2/src)
target_link_libraries(main PRIVATE ${VTK_LIBRARIES})
# vtk_module_autoinit is needed
vtk_module_autoinit(
    TARGETS main
    MODULES ${VTK_LIBRARIES}
)
