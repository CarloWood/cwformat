#include "sys.h"
#include "SourceFile.h"
#include <iterator>
#include <sstream>

void SourceFile::init(std::istream& input)
{
  std::ostringstream ss;
  ss << input.rdbuf();
  content_ = std::move(ss).str();
}

SourceFile::iterator SourceFile::begin() const
{
  return content_.begin();
}

SourceFile::iterator SourceFile::end() const
{
  return content_.end();
}

char SourceFile::peek(SourceFile::iterator pos) const
{
  return *(pos + 1);
}

View SourceFile::range(SourceFile::iterator first, SourceFile::iterator last) const
{
  size_t size = std::distance(first, last);
  return {&*first, size};
}
