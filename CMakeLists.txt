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

find_package(Clang REQUIRED CONFIG)
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR} (found version \"${LLVM_VERSION}\")")

# We don't need this, as Clang already has a dependency on LLVM - and we're
# not using different parts from LLVM that Clang isn't already needing.
#find_package(LLVM REQUIRED CONFIG)
#message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR} (found version \"${LLVM_PACKAGE_VERSION}\")")

add_executable(cwformat cwformat.cxx View.cxx SourceFile.cxx TranslationUnit.cxx NoaContainer.cxx Parser.cxx DiagnosticConsumer.cxx)

#target_compile_definitions(cwformat PRIVATE ${LLVM_DEFINITIONS})
#target_include_directories(cwformat PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(cwformat PRIVATE ${CLANG_INCLUDE_DIRS})

# Manually specify Clang libraries.
set(CLANG_LIBS
    clangBasic
    clangLex
    clangFrontend       # clang::TextDiagnosticPrinter
)

target_link_libraries(cwformat
  PRIVATE
    ${CLANG_LIBS}
    ${AICXX_OBJECTS_LIST}
)
