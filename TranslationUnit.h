#pragma once

#include "NoaContainer.h"
#include "clang/Basic/SourceLocation.h"
#include "debug.h"
#include <memory>

// Forward declarations.
class ClangFrontend;
class SourceFile;

namespace clang {
class Token;
class Preprocessor;
class SourceManager;
} // namespace clang

class TranslationUnit : public NoaContainer
{
 private:
  ClangFrontend& clang_frontend_;
  SourceFile const& source_file_;
  clang::FileID file_id_;                               // The file ID of this translation unit.
  std::unique_ptr<clang::Preprocessor> preprocessor_;   // A preprocessor instance used for this translation unit.
  unsigned int last_offset_;

#ifdef CWDEBUG
  std::string name_;
#endif

 public:
  TranslationUnit(ClangFrontend& clang_frontend, SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name));
  ~TranslationUnit();

  void process(SourceFile const& source_file);
  void add_input_token(clang::SourceLocation current_location, clang::Token const& token, unsigned int current_offset, size_t token_length);
  void eof();

  clang::SourceManager const& source_manager() const;
  SourceFile const& source_file() const { return source_file_; }
  clang::FileID file_id() const { return file_id_; }
  clang::Preprocessor& get_pp() const { return *preprocessor_; }

  std::string const& name() const { return name_; }
  void print(std::ostream& os) const;

 private:
  friend class ClangFrontend;
  // Called from ClangFrontend::begin_source_file.
  void init(clang::FileID file_id, std::unique_ptr<clang::Preprocessor>&& preprocessor);
};
