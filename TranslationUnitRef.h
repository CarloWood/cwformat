#pragma once

#include <clang/Basic/SourceLocation.h>
#include <clang/Lex/Token.h>
#ifdef CWDEBUG
#include "utils/has_print_on.h"
#include <iosfwd>
#endif

#ifdef CWDEBUG
class TranslationUnit;
struct PPToken;

namespace clang {
class MacroDirective;
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

struct MacroDirective
{
  TranslationUnit const& translation_unit_;
  clang::MacroDirective const& macro_directive_;

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
#endif

template<typename TUREF>
class TranslationUnitRefImpl
{
 protected:
  TUREF translation_unit_;

 protected:
  TranslationUnitRefImpl(TUREF translation_unit) : translation_unit_(translation_unit) { }

#ifdef CWDEBUG
 public:
  debug::SourceLocation print_source_location(clang::SourceLocation loc) const { return {translation_unit_, loc}; }
  debug::SourceRange print_source_range(clang::SourceRange const& range) const { return {translation_unit_, range}; }
  debug::CharSourceRange print_char_source_range(clang::CharSourceRange const& char_range) const { return {translation_unit_, char_range}; }
  debug::Token print_token(clang::Token const& token) const { return {translation_unit_, token}; }
  debug::MacroDirective print_macro_directive(clang::MacroDirective const& macro_directive) const { return {translation_unit_, macro_directive}; }
  // Allow calling print_token with a PPToken...
  PPToken const& print_token(PPToken const& token) const { return token; }
#endif
};

using TranslationUnitRef      = TranslationUnitRefImpl<TranslationUnit&>;
using TranslationUnitRefConst = TranslationUnitRefImpl<TranslationUnit const&>;
