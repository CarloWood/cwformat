#include "sys.h"
#include "TranslationUnit.h"
#include "utils/AIAlert.h"
#include <clang/Lex/Preprocessor.h>
#include "ClangFrontend.h"
#include "InputToken.h"
#include "SourceFile.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#include "debug_ostream_operators.h"
#endif
#include "debug.h"
#include "TranslationUnit.inl"

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
  unsigned int end_offset = source_file_.size();
  if (end_offset > last_offset_)
  {
    size_t gap_length = end_offset - last_offset_;
    auto gap_text = source_file_.span(last_offset_, gap_length);

    Dout(dc::notice,
      "End of File Gap: FileOffset: " << last_offset_ << ", Length: " << gap_length << ", Text: '" << buf2str(gap_text.data(), gap_text.size())
                                      << "'");
  }
}

void TranslationUnit::process_gap(unsigned int current_offset)
{
  // Does this ever happen?
  ASSERT(current_offset >= last_offset_);

  if (current_offset > last_offset_)
  {
    unsigned int gap_start = last_offset_;
    size_t gap_length = current_offset - gap_start;
    auto gap_text = source_file_.span(gap_start, gap_length);

    Dout(dc::notice, "Skipped : from offset " << gap_start << ", length: " << gap_length << "; text: '" << buf2str(gap_text) << "'");

    // Scan the gap for whitespace, backslash-newlines, newlines and comments.
    enum LookingForType
    {
      any,                 // Either whitespace or comment start.
      more_whitespace,     // More whitespace.
      comment_start,       // The next character must be a '/' or a '*'.
      c_comment_end_star,  // Looking for a '*'.
      c_comment_end_slash, // Looking for a '/'.
      cpp_comment_end,     // Looking for a '\n'.
      error                // An unrecoverable error occurred.
    };

    unsigned int token_start;
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
            else
              looking_for = error;
          }
          token_start = gap_start + i;
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
      }
      if (looking_for == error)
      {
        // This gap contains a PPToken that should have been detected.
        THROW_ALERT("Gap contains non-whitespace");
      }
    }
    if (looking_for == more_whitespace)
      add_input_token<PPToken>(token_start, gap_start + gap_length - token_start, {PPToken::whitespace});
    else if (looking_for != any)
      // It should not be possible that this happens: we only get here
      // by finding the next clang::Token or preprocessor token, which
      // can't be found inside a comment?!
      THROW_ALERT("Gap contains unterminated comment!");
  }
}

void TranslationUnit::print(std::ostream& os) const
{
  os << "// TranslationUnit: " << name() << "\n";
  NoaContainer::print_real(os);
}
