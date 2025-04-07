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
# Debugging support

# Begin of gitache configuration.
set(GITACHE_PACKAGES libcwd_r)
include(cwm4/cmake/StableGitache)
# End of gitache configuration.

include(cwm4/cmake/AICxxProject)
include(AICxxSubmodules)

#==============================================================================
# cwformat

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_executable(cwformat cwformat.cxx View.cxx SourceFile.cxx TranslationUnit.cxx NoaContainer.cxx Parser.cxx DiagnosticConsumer.cxx)

# Include LLVM directories.
target_compile_definitions(cwformat PRIVATE ${LLVM_DEFINITIONS})
target_include_directories(cwformat PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})

# Get LLVM component libraries (e.g., Core, Support, etc.)
llvm_map_components_to_libnames(LLVM_LIBS
  Support
  Core
  Option
  Demangle)

# Manually specify Clang libraries (correct)
set(CLANG_LIBS
    clangBasic
    clangFrontendTool
    clangLex
    clangSerialization
    # Add other needed Clang libraries:
    # clangFrontend
    # clangParse
    # clangAST
    # clangSema
)

# Link against LLVM and clang libraries.
target_link_libraries(cwformat
  PRIVATE
    ${LLVM_LIBS}
    ${CLANG_LIBS}
    ${AICXX_OBJECTS_LIST}
)

add_executable(test DiagnosticConsumer.cxx)
target_compile_definitions(test PRIVATE ${LLVM_DEFINITIONS})
target_include_directories(test PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(test
  PRIVATE
    ${llvm_libs}
    clang-cpp
    ${AICXX_OBJECTS_LIST}
)
