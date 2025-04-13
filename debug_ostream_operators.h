#pragma once

#include "utils/has_print_on.h"
#include "libcwd/buf2str.h"
#include <iostream>

#ifndef CWDEBUG
#error "Only include debug_ostream_operators.h if CWDEBUG is defined."
#endif

class TranslationUnit;

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

} // namespace clang

namespace debug {
using utils::has_print_on::operator<<;

struct SourceLocation
{
  TranslationUnit const& translation_unit_;
  clang::SourceLocation location_;

  void print_on(std::ostream& os) const;
};

struct SourceRange
{
  TranslationUnit const& translation_unit_;
  clang::SourceRange const& source_range_;

  void print_on(std::ostream& os) const;
};

struct CharSourceRange
{
  TranslationUnit const& translation_unit_;
  clang::CharSourceRange const& char_source_range_;

  void print_on(std::ostream& os) const;
};

struct Token
{
  TranslationUnit const& translation_unit_;
  clang::Token const& token_;

  void print_on(std::ostream& os) const;
};

} // namespace debug

class PrintSourceLocation
{
 private:
  TranslationUnit const& translation_unit_;

 public:
  PrintSourceLocation(TranslationUnit const& translation_unit) : translation_unit_(translation_unit) { }

  debug::SourceLocation operator()(clang::SourceLocation loc) const { return {translation_unit_, loc}; }
};

class PrintSourceRange
{
 private:
  TranslationUnit const& translation_unit_;

 public:
  PrintSourceRange(TranslationUnit const& translation_unit) : translation_unit_(translation_unit) { }

  debug::SourceRange operator()(clang::SourceRange const& range) const { return {translation_unit_, range}; }
};

class PrintCharSourceRange
{
 private:
  TranslationUnit const& translation_unit_;

 public:
  PrintCharSourceRange(TranslationUnit const& translation_unit) : translation_unit_(translation_unit) { }

  debug::CharSourceRange operator()(clang::CharSourceRange const& char_range) const { return {translation_unit_, char_range}; }
};

class PrintToken
{
 private:
  TranslationUnit const& translation_unit_;

 public:
  PrintToken(TranslationUnit const& translation_unit) : translation_unit_(translation_unit) { }

  debug::Token operator()(clang::Token const& token) const { return {translation_unit_, token}; }
};
