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

  llvm::MemoryBufferRef get_memory_buffer_ref() const { return content_->getMemBufferRef(); }
  SourceFile::iterator begin() const { return content_->getBufferStart(); }
  SourceFile::iterator end() const { return content_->getBufferEnd(); }
  size_t size() const { return content_->getBufferSize(); }
  std::string const& filename() const { return filename_; };

  char peek(iterator pos) const;
  View range(iterator first, iterator last) const;
};
