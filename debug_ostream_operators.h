#pragma once

#include "utils/has_print_on.h"
#include "libcwd/buf2str.h"
#include <llvm/ADT/StringRef.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/MacroInfo.h>
#include <iostream>

#ifndef CWDEBUG
#error "Only include debug_ostream_operators.h if CWDEBUG is defined."
#endif

namespace llvm {

inline std::ostream& operator<<(std::ostream& os, StringRef const& str)
{
  return os << '"' << libcwd::buf2str(str) << '"';
}

} // namespace llvm

namespace clang {

// Forward declarations.
class SourceManager;
class SourceLocation;
class SourceRange;
class Token;

namespace SrcMgr {

char const* to_string(CharacteristicKind characteristic_kind);

inline std::ostream& operator<<(std::ostream& os, CharacteristicKind characteristic_kind)
{
  return os << to_string(characteristic_kind);
}

} // namespace SrcMgr

inline std::ostream& operator<<(std::ostream& os, OptionalFileEntryRef opt_file_entry_ref)
{
  if (opt_file_entry_ref.has_value())
    os << opt_file_entry_ref->getName();
  else
    os << "<no file entry>";
  return os;
}

char const* to_string(MacroDirective::Kind macro_directive_kind);

inline std::ostream& operator<<(std::ostream& os, MacroDirective::Kind macro_directive_kind)
{
  return os << to_string(macro_directive_kind);
}

} // namespace clang
