#pragma once

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Token.h"

#include <variant>
#include <optional>
#include <string>

// --- Placeholder for your custom PPToken ---
// You will need to define this struct fully based on your needs.
// It should contain information specific to preprocessor entities
// that aren't standard clang::Tokens (like directives, escaped newlines, etc.)
struct PPToken
{
  // Example: Enum to classify the PP token type
  enum class Kind {
      EscapedNewline,
      Directive, // e.g., #include, #ifdef, #define
      MacroDefinition, // The full definition line(s)
      MacroUsage,      // An invocation of a macro (might overlap with clang::Token::identifier?)
      HeaderName,      // Content within <...> or "..." after #include
      Pragma,
      // ... other PP-specific kinds
  } kind;

  // Example: Store additional data if needed, e.g., the directive name
  std::string directiveName; // Only valid if kind == Kind::Directive
  // ... other potential members like macro parameters, include path etc.

  // Constructor example
  PPToken(Kind k) : kind(k) {}
  PPToken(Kind k, std::string name) : kind(k), directiveName(std::move(name)) {}
  // ... other constructors
};
// --- End Placeholder ---

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
  Payload payload_;                     // The actual data (Token or PPToken).
  std::string_view input_sequence_;     // Start location and length of the input characters in the input buffer.

 public:
  InputToken(clang::Token const& token, std::string_view input_sequence) :
    payload_(token), input_sequence_(input_sequence)
  {
  }

  InputToken(PPToken const& preprocessor_token, std::string_view input_sequence) :
    payload_(preprocessor_token), input_sequence_(input_sequence)
  {
  }
};
