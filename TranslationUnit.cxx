#include "sys.h"
#include "TranslationUnit.h"
#include "utils/AIAlert.h"
#include <clang/Lex/Preprocessor.h>
#include "ClangFrontend.h"
#include "InputToken.h"
#include "SourceFile.h"
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

void TranslationUnit::process(SourceFile const& source_file)
{
  last_offset_ = 0;
  clang_frontend_.process_input_buffer(source_file, *this);
}

clang::SourceManager const& TranslationUnit::source_manager() const
{
  return clang_frontend_.source_manager();
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
  }
}

void TranslationUnit::append_input_token(size_t token_length, PPToken const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::append_input_token(" << token_length << ", " << print_token(token) << ")");

  // We are appending directly after the last token.
  offset_type token_offset = last_offset_;

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

  auto token_view = source_file_.span(token_offset, token_length);
  Dout(dc::notice, "New token to add: \"" << buf2str(token_view) << "\".");

  // Create an InputToken.
  Dout(dc::notice, "Adding " << print_token(token) << " \"" << buf2str(token_view) << "\".");
  InputToken input_token(token, token_view);

  // Update last_offset to the position after the appended token.
  last_offset_ += token_length;
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
std::pair<TranslationUnit::offset_type, size_t> TranslationUnit::process_gap(offset_type current_offset, char const* fixed_string)
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
        THROW_ALERT("Gap contains non-whitespace");
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

void TranslationUnit::print(std::ostream& os) const
{
  os << "// TranslationUnit: " << name() << "\n";
  NoaContainer::print_real(os);
}
