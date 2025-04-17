#include "sys.h"
#include "SourceFile.h"
#include <iterator>
#include <sstream>

std::string_view SourceFile::range(SourceFile::iterator first, SourceFile::iterator last) const
{
  ASSERT(begin() <= first && first <= last && last <= end());
  size_t size = std::distance(first, last);
  return {&*first, size};
}

std::string_view SourceFile::span(SourceFile::iterator first, size_t size) const
{
  ASSERT(begin() <= first && first <= end());
  ASSERT(first + size <= end());
  return {&*first, size};
}

std::string_view SourceFile::span(unsigned int offset, size_t size) const
{
  SourceFile::iterator first = at(offset);
  return span(first, size);
}
