#include "sys.h"
#include "TranslationUnit.h"
#include "SourceFile.h"
#include "ClangFrontend.h"
#include "utils/AIAlert.h"
#include <clang/Lex/Preprocessor.h>
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
  clang_frontend_.process_input_buffer(source_file, *this);
}

void TranslationUnit::add_input_token(clang::Token const& token, unsigned int offset, unsigned int length, unsigned int line, unsigned int col)
{
  DoutEntering(dc::notice, "TranslationUnit::add_input_token(token, " << offset << ", " << length << ", " << line << ", " << col << ")");
  View view = source_file_.span(offset, length);
  Dout(dc::notice, "Token: \"" << view.realize() << "\", line:" << line << ", column: " << col);
}

void TranslationUnit::print(std::ostream& os) const
{
  os << "// TranslationUnit: " << name() << "\n";
  NoaContainer::print_real(os);
}
