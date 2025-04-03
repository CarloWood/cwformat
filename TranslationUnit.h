#pragma once

#include "SourceFile.h"
#include "NoaContainer.h"
#include "debug.h"

class TranslationUnit : public NoaContainer
{
 private:
#ifdef CWDEBUG
  std::string name_;
#endif

 public:
  TranslationUnit(SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name));

  void print(std::ostream& os) const;
};
