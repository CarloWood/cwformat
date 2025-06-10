#include "sys.h"
#include "ClangFrontend.h"
#include "SourceFile.h"
#include "PreprocessorEventsHandler.h"
#include "TranslationUnit.h"
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

void ClangFrontend::process_input_buffer(SourceFile const& source_file, TranslationUnit& translation_unit) const
{
  clang::Preprocessor& pp = translation_unit.get_pp();

  // --- 3. Attach Callbacks ---
  pp.addPPCallbacks(std::make_unique<PreprocessorEventsHandler>(translation_unit));

  // --- 4. Initialize Preprocessor & Configure ---
  pp.Initialize(*target_info_);
  clang::InitializePreprocessor(pp, *preprocessor_options_, *pch_container_reader_ptr_, frontend_options_, code_gen_options_);

//  pp.SetCommentRetentionState(true, true);
  pp.SetSuppressIncludeNotFoundError(false);

  // --- 5. Enter Main File ---
  pp.EnterMainSourceFile();

  // --- 6. The Enhanced Tokenization Loop ---
  Dout(dc::notice, "--- Tokens and Whitespace for " << source_file.filename() << " ---");
  clang::Token tok;

  clang::SourceLocation FileStartLoc = source_manager_.getLocForStartOfFile(translation_unit.file_id());

#ifdef CWDEBUG
  PrintSourceLocation print_source_location(translation_unit);
#endif

  for (;;)
  {
    // --- Lex the next token ---
    pp.Lex(tok);

    // Stop if we reached the end of the file.
    if (tok.is(clang::tok::eof))
    {
      translation_unit.eof();
      break;
    }

    clang::SourceLocation current_location = tok.getLocation();
    Dout(dc::notice, "current_location = " << print_source_location(current_location));

    if (!source_manager_.isInFileID(current_location, translation_unit.file_id()))
    {
      // This token's expansion location is not in the main file (e.g., maybe from a builtin
      // if UsePredefines was true, or it was generated from a macro catenation).

      // Print its info but don't use it for gap calculation.
      clang::SourceLocation spelling_location = source_manager_.getSpellingLoc(current_location);
      char const* tokenName = clang::tok::getTokenName(tok.getKind());
      std::string spelling = pp.getSpelling(tok);

      Dout(dc::notice, "Token(External): "
             << ", Kind: " << tokenName << " (" << tok.getKind() << ")"
             << ", Text: '" << buf2str(spelling.data(), spelling.size()) << "'");
      clang::SourceLocation ExpansionLoc = source_manager_.getExpansionLoc(current_location);
      Dout(dc::notice, "ExpansionLoc = " << print_source_location(ExpansionLoc));
      Dout(dc::notice, "spelling_location = " << print_source_location(spelling_location));

      continue; // Skip gap calculation for this token.
    }

    // Token is in the main file; add the token to the translation unit.
    translation_unit.add_input_token(tok);
  }
  Dout(dc::notice, "--- End of Tokens and Whitespace ---");
}
