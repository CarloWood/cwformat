#pragma once

#include "utils/has_print_on.h"
#include <iostream>

#ifndef CWDEBUG
#error "Only include debug_ostream_operators.h if CWDEBUG is defined."
#endif

namespace debug {
using utils::has_print_on::operator<<;

struct SourceLocation
{
  clang::SourceManager const& source_manager_;
  clang::SourceLocation location_;

  void print_on(std::ostream& os) const
  {
    if (location_.isInvalid())
      os << "<invalid SourceLocation>";
    else if (location_.isMacroID())
      os << "<macro>" << location_.printToString(source_manager_) << "</macro>";
    else
    {
      std::pair<clang::FileID, unsigned int> location = source_manager_.getDecomposedLoc(location_);
      unsigned int line = source_manager_.getLineNumber(location.first, location.second);
      unsigned int column = source_manager_.getColumnNumber(location.first, location.second);
      os << line << ":" << column;
    }
  }
};

} // namespace debug

class PrintSourceLocation
{
 private:
  clang::SourceManager const& source_manager_;

 public:
  PrintSourceLocation(clang::SourceManager const& source_manager) : source_manager_(source_manager) { }

  debug::SourceLocation operator()(clang::SourceLocation loc) const { return {source_manager_, loc}; }
};
