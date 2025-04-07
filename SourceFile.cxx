#include "sys.h"
#include "SourceFile.h"
#include <iterator>
#include <sstream>

SourceFile::iterator SourceFile::begin() const
{
  return content_->getBufferStart();
}

SourceFile::iterator SourceFile::end() const
{
  return content_->getBufferEnd();
}

char SourceFile::peek(SourceFile::iterator pos) const
{
  return pos[1];
}

View SourceFile::range(SourceFile::iterator first, SourceFile::iterator last) const
{
  size_t size = std::distance(first, last);
  return {&*first, size};
}
