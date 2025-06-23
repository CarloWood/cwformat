#include "sys.h"
#include "ClangFrontend.h"
#include "SourceFile.h"
#include "PreprocessorEventsHandler.h"
#include "TranslationUnit.h"
#include "TranslationUnitRef.h"
#include "utils/AIAlert.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Serialization/PCHContainerOperations.h"
#include "clang/Frontend/Utils.h"
#ifdef CWDEBUG
#include "libcwd/buf2str.h"
#include "debug_ostream_operators.h"
#endif

ClangFrontend::ClangFrontend(configure_header_search_options_type configure_header_search_options,
      configure_commandline_macro_definitions_type configure_commandline_macro_definitions) :
    OptionsBase(std::move(configure_header_search_options), std::move(configure_commandline_macro_definitions)),
    diagnostic_consumer_(llvm::errs(), diagnostic_options_.get()), diagnostic_ids_(new clang::DiagnosticIDs),
    diagnostics_engine_(diagnostic_ids_, diagnostic_options_, &diagnostic_consumer_, /*ShouldOwnClient=*/false),
    target_info_(ClangFrontend::create_target_info(diagnostics_engine_, target_options_)), file_manager_(file_system_options_),
    source_manager_(diagnostics_engine_, file_manager_),
    header_search_(header_search_options_, source_manager_, diagnostics_engine_, lang_options_, target_info_.get())
{
  target_info_->adjust(diagnostics_engine_, lang_options_);
  clang::ApplyHeaderSearchOptions(header_search_, header_search_options_, lang_options_, target_info_->getTriple());
}

void ClangFrontend::begin_source_file(SourceFile const& source_file, TranslationUnit& translation_unit)
{
  clang::FileID file_id;

  if (!source_file.full_path().empty())
  {
    clang::Expected<clang::FileEntryRef> fileEntryRefOrErr = file_manager_.getFileRef(source_file.full_path().native());
    ASSERT(fileEntryRefOrErr);
    clang::FileEntryRef fileEntryRef = *fileEntryRefOrErr;
    file_id = source_manager_.createFileID(fileEntryRef, clang::SourceLocation(), clang::SrcMgr::C_User);
    if (file_id.isValid())
      source_manager_.overrideFileContents(fileEntryRef, source_file.get_memory_buffer_ref());
  }
  else
    file_id = source_manager_.createFileID(source_file.get_memory_buffer_ref());
  if (file_id.isInvalid())
    THROW_LALERT("Unable to create FileID for input buffer: [FILENAME]", AIArgs("[FILENAME]", source_file.filename()));
  source_manager_.setMainFileID(file_id);

  auto preprocessor = std::make_unique<clang::Preprocessor>(preprocessor_options_, diagnostics_engine_, lang_options_,
      source_manager_, header_search_, module_loader_, /*IILookup=*/nullptr, /*OwnsHeaderSearch=*/false, clang::TU_Complete);

  diagnostic_consumer_.BeginSourceFile(lang_options_, preprocessor.get());

  translation_unit.init(file_id, std::move(preprocessor));
}

void ClangFrontend::end_source_file()
{
  diagnostic_consumer_.EndSourceFile();
}

//static
clang::TargetInfo* ClangFrontend::create_target_info(
  clang::DiagnosticsEngine& diagnostics_engine, std::shared_ptr<clang::TargetOptions> const& target_options)
{
  clang::TargetInfo* target_info = clang::TargetInfo::CreateTargetInfo(diagnostics_engine, target_options);
  if (!target_info)
    THROW_LALERT("Unable to create target info for triple: [TRIPLE]", AIArgs("[TRIPLE]", target_options->Triple));
  return target_info;
};

void ClangFrontend::process_input_buffer(TranslationUnit& translation_unit) const
{
  clang::Preprocessor& pp = translation_unit.get_pp();

  // Attach preprocessor callbacks.
  pp.addPPCallbacks(std::make_unique<PreprocessorEventsHandler>(translation_unit));

  // Initialize the preprocessor.
  pp.Initialize(*target_info_);
  clang::InitializePreprocessor(pp, *preprocessor_options_, *pch_container_reader_ptr_, frontend_options_, code_gen_options_);
  pp.SetSuppressIncludeNotFoundError(false);

#ifdef CWDEBUG
  PrintSourceLocation print_source_location(translation_unit);
#endif

  // Start processing the source_file.
  pp.EnterMainSourceFile();
  clang::Token tok;
  for (;;)
  {
    // Lex the next token.
    pp.Lex(tok);

    // Stop if we reached the end of the file.
    if (tok.is(clang::tok::eof))
    {
      translation_unit.eof();
      break;
    }

    clang::SourceLocation current_location = tok.getLocation();

    if (!source_manager_.isInFileID(current_location, translation_unit.file_id()))
    {
      // This token's expansion location is not in the main file (e.g., maybe from a builtin
      // if UsePredefines was true, or it was generated from a macro catenation).

      // Print its info but don't use it for gap calculation.
      clang::SourceLocation spelling_location = source_manager_.getSpellingLoc(current_location);
      char const* tokenName = clang::tok::getTokenName(tok.getKind());
      std::string spelling = pp.getSpelling(tok);

#if 0
      Dout(dc::notice, "Token(External): "
             << ", Kind: " << tokenName << " (" << tok.getKind() << ")"
             << ", Text: '" << buf2str(spelling.data(), spelling.size()) << "'");
      clang::SourceLocation ExpansionLoc = source_manager_.getExpansionLoc(current_location);
      Dout(dc::notice, "ExpansionLoc = " << print_source_location(ExpansionLoc));
      Dout(dc::notice, "spelling_location = " << print_source_location(spelling_location));
#endif

      continue; // Skip gap calculation for this token.
    }

    // Token is in the main file; add the token to the translation unit.
    translation_unit.add_input_token(tok);
  }
}

void ClangFrontend::lex_source_range(TranslationUnit& translation_unit, clang::SourceRange range)
{
  DoutEntering(dc::notice, "ClangFrontend::lex_source_range(TranslationUnit:" << translation_unit.name() << ", " << PrintSourceRange{translation_unit}(range) << ")");

  // Unpack the source range.
  clang::SourceLocation RangeBeginLoc = range.getBegin();
  clang::SourceLocation RangeEndLoc = range.getEnd();
  ASSERT(RangeBeginLoc.isValid() && RangeEndLoc.isValid());

  // Get the expansion locations.
  clang::SourceLocation ExpansionLocBegin = source_manager_.getExpansionLoc(RangeBeginLoc);
  clang::SourceLocation ExpansionLocEnd = source_manager_.getExpansionLoc(RangeEndLoc);

#if CW_DEBUG
  {
    clang::FileID FID = source_manager_.getFileID(ExpansionLocBegin);
    ASSERT(FID.isValid());
    // The range must be within one file.
    ASSERT(source_manager_.getFileID(ExpansionLocEnd) == FID);
    // We should only be parsing stuff from the main file.
    ASSERT(FID == translation_unit.file_id());
  }
#endif

  // Pointer to the start of our desired lexing sub-range.
  bool InvalidBuffer = false;
  char const* RangeLexStartPtr = source_manager_.getCharacterData(ExpansionLocBegin, &InvalidBuffer);
  ASSERT(!InvalidBuffer);

  // ExpansionLocEnd points to the START of the last token; we need the location *after* that token.
  // Pointer to the end of our desired lexing sub-range (one past the last char).
  clang::SourceLocation ActualRangeEndLocToken = clang::Lexer::getLocForEndOfToken(ExpansionLocEnd, 0, source_manager_, lang_options_);
  ASSERT(ActualRangeEndLocToken.isValid());

  char const* RangeLexEndPtr = source_manager_.getCharacterData(ActualRangeEndLocToken, &InvalidBuffer);
  ASSERT(!InvalidBuffer);

  // Get the full buffer for the FileID.
  llvm::StringRef FileBuffer = source_manager_.getBufferData(translation_unit.file_id(), &InvalidBuffer);
  ASSERT(!InvalidBuffer && FileBuffer.data() != nullptr);

  return lex_source_range(translation_unit, RangeLexStartPtr, RangeLexEndPtr - RangeLexStartPtr, FileBuffer);
}

void ClangFrontend::lex_source_range(TranslationUnit& translation_unit, TranslationUnit::offset_type offset, size_t range_size)
{
}

void ClangFrontend::lex_source_range(TranslationUnit& translation_unit, char const* RangeLexStartPtr, size_t range_size, llvm::StringRef FileBuffer)
{
  DoutEntering(dc::notice, "ClangFrontend::lex_source_range(TranslationUnit:" << translation_unit.name() << ", ⟪" << buf2str(RangeLexStartPtr, range_size) << "⟫, FileBuffer)");

  char const* FileBufStart = FileBuffer.data();
  char const* FileBufEnd = FileBufStart + FileBuffer.size();
  // Ensure the range does not go beyond the file buffer (sanity check).
  ASSERT(range_size <= FileBufEnd - RangeLexStartPtr);

  clang::SourceLocation FileStartLocForLexer = source_manager_.getLocForStartOfFile(translation_unit.file_id());
  // We must use FileBufEnd (as opposed to RangeLexEndPtr) because the range must be nul-terminated.
  clang::Lexer SubLexer(FileStartLocForLexer, lang_options_, FileBufStart, RangeLexStartPtr, FileBufEnd);

  Dout(dc::notice, "Lexing sub-range:");
  {
#ifdef CWDEBUG
    NAMESPACE_DEBUG::Indent indent(2);
#endif
    bool found_function_like_macro = false;
    int macro_parens = 0;
    TranslationUnit::offset_type const range_end_offset = (RangeLexStartPtr - FileBufStart) + range_size;
    TranslationUnit::offset_type token_offset;
    clang::Token Tok;
    for (;;)
    {
      SubLexer.LexFromRawLexer(Tok);    // Gets raw tokens, no macro expansion.
      Dout(dc::notice, "found: " << debug::Token{translation_unit, Tok});
      // Was the previous token a macro invocation?
      if (found_function_like_macro)
      {
        found_function_like_macro = false;
        if (Tok.getKind() == clang::tok::l_paren)        // This should always be true (it is a function-like macro).
        {
          macro_parens = 1;
          // Do not add the parenthesis or arguments of function-like macros in this loop.
          continue;
        }
      }
      else if (macro_parens > 0)
      {
        if (Tok.getKind() == clang::tok::l_paren)
          ++macro_parens;
        else if (Tok.getKind() == clang::tok::r_paren)
          --macro_parens;
        // Skip everything inbetween function-like macro parenthesis.
        continue;
      }
      token_offset = source_manager_.getFileOffset(Tok.getLocation());
      // Bail out if this is already past the last token of the range.
      // This always works, even if the end of the range coincides with the end of the file,
      // because there will always be at least an <eof> token after our range, which has the
      // size of the file as offset.
      if (token_offset >= range_end_offset)
        break;
      if (Tok.getKind() == clang::tok::raw_identifier)
      {
        if (auto result = translation_unit.is_next_queued_macro(token_offset))
        {
          Dout(dc::notice, "This is the next macro!");
          found_function_like_macro = result->kind_ == PPToken::function_macro_invocation_name;
          // Do not add macros in this loop.
          continue;
        }
      }
      translation_unit.add_input_token(Tok);
    }
  }
  Dout(dc::notice, "Finished lexing sub-range.");
}
