#pragma once

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Token.h"
#include <optional>
#include <string>
#include <variant>

#ifdef CWDEBUG
#include "utils/has_print_on.h"
#endif

#ifdef CWDEBUG
using utils::has_print_on::operator<<;
#endif

struct PPToken
{
  enum Kind
  {
    whitespace,
    c_comment,
    cxx_comment,
    escaped_newline,    // A "\\\n".
    directive_hash,     // The '#' of directives.
    directive,          // e.g., include, ifdef, ifndef, else, elif, endif, define, pragma ...
    macro_name,         // The name of a macro that is being defined that is not a function.
    function_macro_name,// The name of a macro that is being defined that is a function.
    function_macro_lparen,      // The opening parenthesis of a function-like macro definition.
    function_macro_rparen,      // The closing parenthesis of a function-like macro definition.
    function_macro_param,       // Function-like macro parameter identifier.
    function_macro_comma,       // Function-like macro parameter separator.
//  macro_definition,   // The full definition line(s)
//  macro_usage,        // An invocation of a macro (might overlap with clang::Token::identifier?)
    header_name,        // The <...> or "..." that follows an #include.
    pragma,             // What follows a #pragma.
    // ... other PP-specific kinds
  } kind_;

  //  std::string content_; // Only valid if kind_ is Directive, HeaderName, Pragma.

  // Constructor example
  PPToken(Kind k) : kind_(k) {}
  //  PPToken(Kind k, std::string content) : kind_(k), content_(std::move(content)) {}

  inline char const* getTokenName(PPToken::Kind kind) const;

#ifdef CWDEBUG
  void print_on(std::ostream& os) const;
#endif
};

char const* to_string(PPToken::Kind kind);

char const* PPToken::getTokenName(PPToken::Kind kind) const
{
  return to_string(kind);
}

//
// InputToken
//
// Represents a contiguous segment of raw text from the source buffer, corresponding to
// either a lexical token (clang::Token) or a preprocessor construct (PPToken).
//
// Stores the starting location and raw character length of how the token appears in the source buffer.
//
class InputToken
{
 public:
  using Payload = std::variant<clang::Token, PPToken>;

 private:
  Payload payload_;                 // The actual data (Token or PPToken).
  std::string_view input_sequence_; // Start location and length of the input characters in the input buffer.

 public:
  InputToken(clang::Token const& token, std::string_view input_sequence) : payload_(token), input_sequence_(input_sequence) { }
  InputToken(PPToken const& preprocessor_token, std::string_view input_sequence) : payload_(preprocessor_token), input_sequence_(input_sequence) { }
};
