cmake_minimum_required(VERSION 3.14...3.31)

project(cwformat
  LANGUAGES C CXX
  DESCRIPTION "C++ formatter."
)

# Compile all subdirectories with the same standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#==============================================================================
# cwformat

find_package(LLVM REQUIRED CONFIG)
find_package(Boost CONFIG COMPONENTS filesystem)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_executable(cwformat cwformat.cxx View.cxx SourceFile.cxx)

# Include LLVM directories.
target_compile_definitions(cwformat PRIVATE DEBUG ${LLVM_DEFINITIONS})
target_include_directories(cwformat PRIVATE ${LLVM_INCLUDE_DIRS})

# Find the libraries that correspond to the LLVM components that we wish to use.
llvm_map_components_to_libnames(llvm_libs support)
# Link against LLVM libraries.
target_link_libraries(cwformat
  PRIVATE
    ${llvm_libs}
  PUBLIC
    Boost::filesystem
)
