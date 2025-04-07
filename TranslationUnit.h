#pragma once

#include "SourceFile.h"
#include "NoaContainer.h"
#include "debug.h"

class TranslationUnit : public NoaContainer
{
 private:
  SourceFile source_file_;

#ifdef CWDEBUG
  std::string name_;
#endif

 public:
  TranslationUnit(SourceFile&& source_file COMMA_CWDEBUG_ONLY(std::string const& name));

  SourceFile const& source_file() const { return source_file_; }

  void print(std::ostream& os) const;
};
