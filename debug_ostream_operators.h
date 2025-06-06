#pragma once

#include "TranslationUnitRef.h"
#include "utils/has_print_on.h"
#include "libcwd/buf2str.h"
#include <llvm/ADT/StringRef.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/PPCallbacks.h>
#include <iostream>

#ifndef CWDEBUG
#error "Only include debug_ostream_operators.h if CWDEBUG is defined."
#endif

namespace debug {
using utils::has_print_on::operator<<;

struct MacroInfo : public TranslationUnitRefConst
{
 private:
  clang::MacroInfo const& macro_info_;

 public:
  MacroInfo(TranslationUnit const& translation_unit, clang::MacroInfo const& macro_info) :
    TranslationUnitRefConst(translation_unit), macro_info_(macro_info) { }

  void print_on(std::ostream& os) const;
};

struct IdentifierInfo : public TranslationUnitRefConst
{
 private:
  clang::IdentifierInfo const& identifier_info_;

 public:
  IdentifierInfo(TranslationUnit const& translation_unit, clang::IdentifierInfo const& identifier_info) :
    TranslationUnitRefConst(translation_unit), identifier_info_(identifier_info) { }

  void print_on(std::ostream& os) const;
};

} // namespace debug

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
class MacroInfo;

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

char const* to_string(PPCallbacks::FileChangeReason file_change_reason);

inline std::ostream& operator<<(std::ostream& os, PPCallbacks::FileChangeReason file_change_reason)
{
  return os << to_string(file_change_reason);
}

char const* to_string(PPCallbacks::LexedFileChangeReason lexed_file_change_reason);

inline std::ostream& operator<<(std::ostream& os, PPCallbacks::LexedFileChangeReason lexed_file_change_reason)
{
  return os << to_string(lexed_file_change_reason);
}

} // namespace clang
