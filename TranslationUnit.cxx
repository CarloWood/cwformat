#include "sys.h"
#include "TranslationUnit.h"

TranslationUnit::TranslationUnit(SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name)) CWDEBUG_ONLY(: name_(name))
{
}

void TranslationUnit::print(std::ostream& os) const
{
}
