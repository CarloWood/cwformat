#include "sys.h"
#include "ClangFrontend.h"
#include "SourceFile.h"
#include "TranslationUnit.h"
#include "InputToken.h"
#include "utils/AIAlert.h"
#include <clang/Lex/Preprocessor.h>
#ifdef CWDEBUG
#include "debug_ostream_operators.h"
#include <libcwd/buf2str.h>
#endif
#include "debug.h"

TranslationUnit::TranslationUnit(ClangFrontend& clang_frontend, SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name)) :
  clang_frontend_(clang_frontend), source_file_(source_file) COMMA_CWDEBUG_ONLY(name_(name))
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

void TranslationUnit::add_input_token(clang::SourceLocation current_location, clang::Token const& token, unsigned int current_offset, size_t token_length)
{
  PrintSourceLocation const print_source_location{clang_frontend_.source_manager()};

  DoutEntering(dc::notice, "TranslationUnit::add_input_token(" << print_source_location(current_location) << ", token, " << current_offset << ", " << token_length << ")");

  auto token_view = source_file_.span(current_offset, token_length);
  Dout(dc::notice, "Token: \"" << buf2str(token_view) << "\"");

  // Does this ever happen?
  ASSERT(current_offset >= last_offset_);

  // Whitespace recovery.
  if (current_offset > last_offset_)
  {
    size_t gap_length = current_offset - last_offset_;
    auto gap_text = source_file_.span(last_offset_, gap_length);

    Dout(dc::notice, "Gap : FileOffset: " << last_offset_ << ", Length: " << gap_length << ", Text: '" << buf2str(gap_text) << "'");
  }

  // Create an InputToken.
  InputToken input_token(token, token_view);

  // Update last_offset to the position after the current token.
  last_offset_ = current_offset + token_length;
}

void TranslationUnit::eof()
{
  // Process any remaining gap at the end of the file.
  unsigned int end_offset = source_file_.size();
  if (end_offset > last_offset_)
  {
    size_t gap_length = end_offset - last_offset_;
    auto gap_text = source_file_.span(last_offset_, gap_length);

    Dout(dc::notice, "End of File Gap: FileOffset: " << last_offset_ << ", Length: " << gap_length << ", Text: '" << buf2str(gap_text.data(), gap_text.size()) << "'");
  }
}

void TranslationUnit::print(std::ostream& os) const
{
  os << "// TranslationUnit: " << name() << "\n";
  NoaContainer::print_real(os);
}
