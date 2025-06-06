#pragma once

#include <string>
#include <llvm/Support/MemoryBuffer.h>
#include <string_view>
#include <filesystem>
#include "debug.h"

// A C++ source file.
class SourceFile
{
 public:
  using iterator = char const*;

 private:
  std::string filename_;
  std::filesystem::path full_path_;
  std::unique_ptr<llvm::MemoryBuffer> content_;

 public:
  SourceFile() = default;
  SourceFile(std::string const& filename, std::filesystem::path const& full_path,
      std::unique_ptr<llvm::MemoryBuffer> input_buffer) : filename_(filename), full_path_(full_path), content_(std::move(input_buffer))  { }

  llvm::MemoryBufferRef get_memory_buffer_ref() const { return content_->getMemBufferRef(); }
  SourceFile::iterator begin() const { return content_->getBufferStart(); }
  SourceFile::iterator end() const { return content_->getBufferEnd(); }
  size_t size() const { return content_->getBufferSize(); }
  std::string const& filename() const { return filename_; };
  std::filesystem::path const& full_path() const { return full_path_; }
  SourceFile::iterator at(unsigned int offset) const
  {
    SourceFile::iterator position = content_->getBufferStart() + offset;
    ASSERT(position < content_->getBufferEnd());
    return position;
  }

  std::string_view range(iterator first, iterator last) const;
  std::string_view span(SourceFile::iterator first, size_t size) const;
  std::string_view span(unsigned int offset, size_t size) const;
};
