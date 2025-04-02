#pragma once

#include "View.h"
#include <string>

// A C++ source file.
class SourceFile
{
 public:
  using iterator = std::string::const_iterator;

 private:
  std::string filename_;
  std::string content_;         // For now, just read everything into one big contiguous string.

 public:
  SourceFile() = default;
  SourceFile(std::string const& filename, std::istream& input) : filename_(filename) { init(input); }

  void init(std::istream& input);

  iterator begin() const;
  iterator end() const;

  char peek(iterator pos) const;
  std::string const& filename() const { return filename_; };

  View range(iterator first, iterator last) const;
};
