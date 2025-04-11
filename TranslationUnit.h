#pragma once

#include "NoaContainer.h"
#include "clang/Basic/SourceLocation.h"
#include "debug.h"

class ClangFrontend;
class SourceFile;

class TranslationUnit : public NoaContainer
{
 private:
  ClangFrontend const& clang_frontend_;
  clang::FileID file_id_;                           // The file ID of this translation unit.

#ifdef CWDEBUG
  std::string name_;
#endif

 public:
  TranslationUnit(ClangFrontend& clang_frontend, SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name));

  void process(SourceFile const& source_file);

  clang::FileID file_id() const { return file_id_; }

  std::string const& name() const { return name_; }
  void print(std::ostream& os) const;
};
