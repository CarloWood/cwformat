#include "sys.h"
#include "TranslationUnit.h"
#include "SourceFile.h"
#include "ClangFrontend.h"
#include "utils/AIAlert.h"

TranslationUnit::TranslationUnit(ClangFrontend& clang_frontend, SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name)) :
  clang_frontend_(clang_frontend), file_id_(clang_frontend.create_file_id(source_file)) COMMA_CWDEBUG_ONLY(name_(name))
{
  if (file_id_.isInvalid())
    THROW_LALERT("Unable to create FileID for input buffer: [FILENAME]", AIArgs("[FILENAME]", source_file.filename()));
  clang_frontend.set_main_file_id(file_id_);
}

void TranslationUnit::process(SourceFile const& source_file)
{
  clang_frontend_.process_input_buffer(source_file, *this);
}

void TranslationUnit::print(std::ostream& os) const
{
  os << "// TranslationUnit: " << name() << "\n";
  NoaContainer::print_real(os);
}
