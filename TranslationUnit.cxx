#include "sys.h"
#include "TranslationUnit.h"
#include "CodeScanner.h"
#include "utils/AIAlert.h"
#include <clang/Lex/Preprocessor.h>
#include "ClangFrontend.h"
#include "InputToken.h"
#include "SourceFile.h"
#include <ranges>
#ifdef CWDEBUG
#include "utils/print_pointer.h"
#include <libcwd/buf2str.h>
#include "debug_ostream_operators.h"
#endif
#include "debug.h"

TranslationUnit::TranslationUnit(ClangFrontend& clang_frontend, SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name)) :
    CWDEBUG_ONLY(TranslationUnitRef(*this), ) clang_frontend_(clang_frontend), source_file_(source_file) COMMA_CWDEBUG_ONLY(name_(name))
{
  clang_frontend_.begin_source_file(source_file, *this);
}

TranslationUnit::~TranslationUnit()
{
  clang_frontend_.end_source_file();
}

void TranslationUnit::init(clang::FileID file_id, std::unique_ptr<clang::Preprocessor>&& preprocessor)
{
  file_id_ = file_id;
  preprocessor_ = std::move(preprocessor);
}

void TranslationUnit::process()
{
  last_offset_ = 0;
  clang_frontend_.process_input_buffer(*this);
}

void TranslationUnit::eof()
{
  // Process any remaining gap at the end of the file.
  offset_type end_offset = source_file_.size();
  if (end_offset > last_offset_)
  {
    size_t gap_length = end_offset - last_offset_;
    auto gap_text = source_file_.span(last_offset_, gap_length);

    Dout(dc::notice,
      "End of File Gap: FileOffset: " << last_offset_ << ", Length: " << gap_length << ", Text: '" << buf2str(gap_text.data(), gap_text.size())
                                      << "'");
    process_gap(end_offset);
  }
}

void TranslationUnit::append_input_token(size_t token_length, PPToken const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::append_input_token(" << token_length << ", " << print_token(token) << ")");

  // We are appending directly after the last token.
  offset_type const token_offset = last_offset_;

  // However, backslash-newlines are always allowed. Include any with this token.
  auto current = source_file_.at(last_offset_);
  auto end = source_file_.end();
  while (current != end && *current == '\\')
  {
    if (++current != end && *current == '\n')
    {
      token_length += 2;
      ++current;
    }
    else
    {
      --current;
      break;
    }
  }

  add_input_token(last_offset_, token_length, token, false);
}

// Finds all whitespace, C-comment and C++-comment character sequences (all possibly having backslash-newlines inserted)
// up till but not including current_offset (which is where a new token was found that isn't either of those three).
// If fixed_string is non-null, then this function stops when it encounters this string (not inside a comment) and
// returns the offset at which this fixed_string starts, plus its length (which can be larger than the size of
// fixed_string due to inserted backslash-newlines). If fixed_string is non-null and it can not be found, then
// an exception is thrown (this is a fatal error that should never happen).
//
// fixed_string must begin with a character that is not whitespace or a slash.
//
std::pair<TranslationUnit::offset_type, size_t> TranslationUnit::process_gap(offset_type const current_offset, char const* fixed_string)
{
  DoutEntering(dc::notice, "TranslationUnit::process_gap(" << current_offset << ", " << debug::print_string(fixed_string) << ")");

  // Does this ever happen?
  ASSERT(current_offset >= last_offset_);

  if (current_offset > last_offset_)
  {
    offset_type gap_start = last_offset_;
    size_t gap_length = current_offset - gap_start;
    auto gap_text = source_file_.span(gap_start, gap_length);

    Dout(dc::notice, "Skipped : from offset " << gap_start << ", length: " << gap_length << "; text: '" << buf2str(gap_text) << "'");

    // Check if we still have to decode the macro arguments of a previous function-like macro invocation.
    if (last_token_was_function_macro_invocation_name_)
    {
      last_token_was_function_macro_invocation_name_ = false;

      CodeScanner scanner(gap_text);
      std::vector<LParenCommaRParen> const& parens_and_commas = scanner.parens_and_commas();
      ASSERT(parens_and_commas.size() >= 2);    // There should at least be the opening and closing parenthesis.
      ASSERT(parens_and_commas.front().kind_ == LParenCommaRParen::lparen);  // The first one must be the opening parenthesis.
      ASSERT(parens_and_commas.back().kind_ == LParenCommaRParen::rparen);   // The last one must be the closing parenthesis.

      // Consider the code
      //
      // initial pos:     ptr                      ptr                      ptr                      ptr
      //                   |                        |                        |                        |
      //                   v                        v                        v                        v
      // MY_MACRO<--gap1-->(<--gap2-->arg1<--gap3-->,<--gap4-->arg2<--gap5-->,<--gap6-->arg3<--gap7-->)<--gap8-->next_thing
      //                              ^  ^
      //                              |  |
      //                      arg_start  arg_end

      // Abbreviations.
      PPToken::Kind lparen = PPToken::function_macro_invocation_lparen;
      PPToken::Kind comma  = PPToken::function_macro_invocation_comma;
      PPToken::Kind rparen = PPToken::function_macro_invocation_rparen;
      std::vector<LParenCommaRParen>::const_iterator ptr = parens_and_commas.begin();   // Points to the '('.
      for (PPToken::Kind ptr_kind = lparen;; ptr_kind = ptr->kind_ == LParenCommaRParen::comma ? comma : rparen)
      {
        // Add the character that `ptr` is pointing to. This adds '<--gap{N}-->' and the '(', ',' or ')' that follows.
        add_input_token<PPToken>(gap_start + ptr->offset_, 1, {ptr_kind});
        if (ptr_kind == rparen) // Are we done?
          break;
        // Create an CodeScanner::iterator that points to the '(' or ',' on the left of the target argument and then advance it to the start of that argument.
        CodeScanner::iterator arg_start(scanner, ptr->offset_);
        ++arg_start;
        // Create an CodeScanner::iterator that points to the ',' or ')' on the right of the target argument and then retreat it to the end of that argument.
        CodeScanner::iterator arg_end(scanner, (++ptr)->offset_);
        --arg_end;
        // Add '<--gap{N+1}-->' and 'arg{N}'.
        add_input_token<PPToken>(gap_start + arg_start.offset(), arg_end - arg_start + 1, {PPToken::function_macro_invocation_arg});
      }

      // Re-initialize the remaining gap before falling through.
      gap_start = last_offset_;
      gap_length = current_offset - gap_start;
      gap_text = source_file_.span(gap_start, gap_length);
    }

    // Scan the gap for whitespace, backslash-newlines, newlines and comments.
    enum LookingForType
    {
      any,                 // Either whitespace, comment start or fixed_string.
      more_whitespace,     // More whitespace.
      more_fixed_string,   // Consuming the fixed string.
      comment_start,       // The next character must be a '/' or a '*'.
      c_comment_end_star,  // Looking for a '*'.
      c_comment_end_slash, // Looking for a '/'.
      cpp_comment_end,     // Looking for a '\n'.
      error                // An unrecoverable error occurred.
    };

    char expected;
    size_t fixed_string_length;
    int j;
    offset_type token_start;
    LookingForType looking_for = any;
    for (unsigned int i = 0; i < gap_length; ++i)
    {
      char c = gap_text[i];
      switch (looking_for)
      {
        case any:
        case more_whitespace:
          if (std::isspace(static_cast<unsigned char>(c)) ||
            // Consider a backslash-newline to be whitespace too. Don't bother to increment `i`
            // (which would interfer with determining token_start below): the next character
            // is a newline, which is whitespace anyway.
            (c == '\\' && i < gap_length - 1 && gap_text[i + 1] == '\n'))
          {
            if (looking_for == more_whitespace)
              break;
            // We found the start of white space.
            looking_for = more_whitespace;
          }
          else
          {
            if (looking_for == more_whitespace)
              add_input_token<PPToken>(token_start, gap_start + i - token_start, {PPToken::whitespace});
            if (c == '/')
              // We found the start of a comment.
              looking_for = comment_start;
            else if (fixed_string && c == fixed_string[0])      // Note that fixed_string, if non-null, is never empty.
            {
              fixed_string_length = std::strlen(fixed_string);
              if (fixed_string_length == 1)
              {
                std::pair<offset_type, size_t> result{gap_start + i, 1};
                Dout(dc::notice, "returning " << result);
                return result;
              }
              // We found the fixed string.
              looking_for = more_fixed_string;
              expected = fixed_string[j = 1];
            }
            else
              looking_for = error;
          }
          token_start = gap_start + i;
          break;
        case more_fixed_string:
          if (c == expected)
          {
            if (++j == fixed_string_length - 1)
            {
              std::pair<offset_type, size_t> result{token_start, gap_start + i - token_start + 2};
              Dout(dc::notice, "returning " << result);
              return result;
            }
            expected = fixed_string[j];
          }
          else if (c == '\\' && i < gap_length - 1 && gap_text[i + 1] == '\n')
          {
            // Skip the newline.
            ++i;
          }
          else
            looking_for = error;
          break;
        case comment_start:
          if (c == '/')      // This is a C++ comment.
            looking_for = cpp_comment_end;
          else if (c == '*') // This is a C comment.
            looking_for = c_comment_end_star;
          else
            looking_for = error;
          break;
        case c_comment_end_star:
          if (c == '*')
            looking_for = c_comment_end_slash;
          else
            looking_for = c_comment_end_star;
          break;
        case c_comment_end_slash:
          if (c == '/')
          {
            add_input_token<PPToken>(token_start, gap_start + i - token_start + 1, {PPToken::c_comment});
            looking_for = any;
          }
          else
            looking_for = c_comment_end_star;
          break;
        case cpp_comment_end:
          if (c == '\n')
          {
            add_input_token<PPToken>(token_start, gap_start + i - token_start + 1, {PPToken::cxx_comment});
            looking_for = any;
          }
          else
            looking_for = cpp_comment_end;
          break;
        case error:
          AI_NEVER_REACHED
      }
      if (looking_for == error)
      {
        // This gap contains a PPToken that should have been detected.
        gap_text.remove_prefix(i);
        THROW_ALERT("Gap contains non-whitespace at [ERROR_LOCATION]", AIArgs("[ERROR_LOCATION]", libcwd::buf2str(gap_text)));
      }
    }
    if (looking_for == more_whitespace)
      add_input_token<PPToken>(token_start, gap_start + gap_length - token_start, {PPToken::whitespace});
    else if (looking_for == more_fixed_string)
      THROW_ALERT("Gap does not contain the required fixed string \"[FIXED_STRING]\"!", AIArgs("[FIXED_STRING]", fixed_string));
    else if (looking_for != any)
      // It should not be possible that this happens: we only get here
      // by finding the next clang::Token or preprocessor token, which
      // can't be found inside a comment?!
      THROW_ALERT("Gap contains unterminated comment!");
  }

  // No fixed_string, so the return value isn't used.
  return {};
}

void TranslationUnit::add_input_token(clang::CharSourceRange char_source_range, PPToken const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::add_input_token(" << print_char_source_range(char_source_range) << ", " << print_token(token) << ")");

  clang::SourceManager const& source_manager = clang_frontend_.source_manager();
  offset_type begin_offset = source_manager.getFileOffset(char_source_range.getBegin());
  offset_type end_offset = source_manager.getFileOffset(char_source_range.getEnd());
  if (char_source_range.isTokenRange())
  {
    auto [last_token_offset, last_token_length] = clang_frontend_.measure_token_length(char_source_range.getEnd());
    end_offset += last_token_length;
  }
  add_input_token(begin_offset, end_offset - begin_offset, token);
}

#if 0
// Insert token0 followed by token1 by searching the gap produced by token1 backwards, looking for the last occurence of token0 which must be a fixed string.
void TranslationUnit::add_input_tokens(char const* fixed_string, PPToken const& token0, clang::SourceLocation token1_location, PPToken const& token1)
{
  DoutEntering(dc::notice, "TranslationUnit::add_input_tokens(" << debug::print_string(fixed_string) << ", " << print_token(token0) << ", " <<
      print_source_location(token1_location) << ", " << print_token(token1));

  // Get offset and length of token1.
  auto [token1_offset, token1_length] = clang_frontend_.measure_token_length(token1_location);

  // Get the gap produced by token1.
  offset_type gap_start = last_offset_;
  size_t gap_length = token1_offset - gap_start;
  auto gap_text = source_file_.span(gap_start, gap_length);

  // Analyse the gap.
  CodeScanner scanner(gap_text);

  // Create an iterator that points one passed the end of the gap.
  CodeScanner::iterator iter(scanner, token1_offset);
  --iter;
}
#endif

void TranslationUnit::print(std::ostream& os) const
{
  os << "// TranslationUnit: " << name() << "\n";
  NoaContainer::print_real(os);
}
