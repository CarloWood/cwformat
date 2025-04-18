#pragma once

#include "ClangFrontend.h"
#include "TranslationUnitRef.h"
#include "InputToken.h"
#include "NoaContainer.h"
#include "clang/Basic/SourceLocation.h"
#include <memory>
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif
#include "debug.h"

// Forward declarations.
class SourceFile;
class PreprocessorEventsHandler;
struct PPToken;

namespace clang {
class Token;
class Preprocessor;
class SourceManager;
} // namespace clang

class TranslationUnit : public NoaContainer COMMA_CWDEBUG_ONLY(public TranslationUnitRef)
{
 public:
  using offset_type = unsigned int;

 private:
  ClangFrontend& clang_frontend_;
  SourceFile const& source_file_;
  clang::FileID file_id_;                               // The file ID of this translation unit.
  std::unique_ptr<clang::Preprocessor> preprocessor_;   // A preprocessor instance used for this translation unit.
  offset_type last_offset_;

#ifdef CWDEBUG
  std::string name_;
#endif

 public:
  TranslationUnit(ClangFrontend& clang_frontend, SourceFile const& source_file COMMA_CWDEBUG_ONLY(std::string const& name));
  ~TranslationUnit();

  void process(SourceFile const& source_file);
  void eof();

  template<typename TOKEN>
  requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
  void add_input_token(clang::SourceLocation token_location, TOKEN const& token);

  template<typename TOKEN>
  requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
  void add_input_token(clang::SourceRange token_range, TOKEN const& token);

  template<typename TOKEN>
  requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
  void add_input_token(offset_type token_offset, size_t token_length, TOKEN const& token);

  // Append a token without allowing whitespace (except backslash-newlines).
  void append_input_token(size_t token_length, PPToken const& token);

  // Add expected fixed_string as token (possibly adding preceding whitespace first).
  void add_input_token(char const* fixed_string, PPToken const& token)
  {
    auto [token_offset, token_length] = process_gap(static_cast<offset_type>(source_file_.size()), fixed_string);
    append_input_token(token_length, token);
  }

  clang::SourceManager const& source_manager() const;
  SourceFile const& source_file() const { return source_file_; }
  clang::FileID file_id() const { return file_id_; }
  clang::Preprocessor& get_pp() const { return *preprocessor_; }

#ifdef CWDEBUG
  std::string const& name() const { return name_; }
#endif
  void print(std::ostream& os) const;

 private:
  friend class ClangFrontend;
  // Called from ClangFrontend::begin_source_file.
  void init(clang::FileID file_id, std::unique_ptr<clang::Preprocessor>&& preprocessor);

  friend class PreprocessorEventsHandler;
  // Called from add_input_token and PreprocessorEventsHandler::MacroDefined.
  std::pair<offset_type, size_t> process_gap(offset_type token_offset, char const* fixed_string = nullptr);
};

template<typename TOKEN>
requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
void TranslationUnit::add_input_token(clang::SourceLocation token_location, TOKEN const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::add_input_token(" << print_source_location(token_location) << ", " << print_token(token) << ")");

  auto [token_offset, token_length] = clang_frontend_.measure_token_length(token_location);
  add_input_token(token_offset, token_length, token);
}

template<typename TOKEN>
requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
void TranslationUnit::add_input_token(clang::SourceRange token_range, TOKEN const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::add_input_token(" << print_source_range(token_range) << ", " << print_token(token) << ")");

  clang::SourceManager const& source_manager = clang_frontend_.source_manager();
  offset_type begin_offset = source_manager.getFileOffset(token_range.getBegin());
  offset_type end_offset = source_manager.getFileOffset(token_range.getEnd());
  add_input_token(begin_offset, end_offset - begin_offset, token);
}

template<typename TOKEN>
requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
void TranslationUnit::add_input_token(offset_type token_offset, size_t token_length, TOKEN const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::add_input_token(" << token_offset << ", " << token_length << ", " << print_token(token) << ")");

  // All tokens in the source file must be processed in the order the appear in the file.
  ASSERT(token_offset >= last_offset_);

  auto token_view = source_file_.span(token_offset, token_length);
  Dout(dc::notice, "New token to add: \"" << buf2str(token_view) << "\".");

  // Process the characters that were skipped (whitespace and comments- plus optional backslash-newlines).
  process_gap(token_offset);

  // Create an InputToken.
  Dout(dc::notice, "Adding " << print_token(token) << " \"" << buf2str(token_view) << "\".");
  InputToken input_token(token, token_view);

  // Update last_offset to the position after the current token.
  last_offset_ = token_offset + token_length;
}
