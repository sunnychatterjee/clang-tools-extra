set(LLVM_LINK_COMPONENTS
  Support
  )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17 /wd4996")

add_clang_executable(call-counter
  call_counter.cpp
  call_counter_main.cpp
  )

target_link_libraries(call-counter
  PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangFrontend
  clangTooling
  clangToolingRefactor
  )
