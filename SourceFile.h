#pragma once

#include "View.h"
#include "llvm/Support/MemoryBuffer.h"
#include <string>

// A C++ source file.
class SourceFile
{
 public:
  using iterator = char const*;

 private:
  std::string filename_;
  std::unique_ptr<llvm::MemoryBuffer> content_;

 public:
  SourceFile() = default;
  SourceFile(std::string const& filename, std::unique_ptr<llvm::MemoryBuffer> input_buffer) : filename_(filename), content_(std::move(input_buffer))  { }

  iterator begin() const;
  iterator end() const;

  char peek(iterator pos) const;
  std::string const& filename() const { return filename_; };
  size_t size() const { return content_->getBufferSize(); }

  View range(iterator first, iterator last) const;
};
