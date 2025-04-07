#include "sys.h"
#include "TranslationUnit.h"

TranslationUnit::TranslationUnit(SourceFile&& source_file COMMA_CWDEBUG_ONLY(std::string const& name)) :
  source_file_(std::move(source_file)) COMMA_CWDEBUG_ONLY(name_(name))
{
}

void TranslationUnit::print(std::ostream& os) const
{
  os << "// Source file: " << source_file_.filename() << "\n";
  NoaContainer::print_real(os);
}
