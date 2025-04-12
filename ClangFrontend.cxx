#include "sys.h"
#include "ClangFrontend.h"
#include "SourceFile.h"
#include "libcwd/buf2str.h"
#include "clang/Lex/Preprocessor.h"
#include "utils/AIAlert.h"
#include "MacroCallbackRecorder.h"

ClangFrontend::ClangFrontend() :
    diagnostic_ids_(new clang::DiagnosticIDs), diagnostic_consumer_(llvm::errs(), diagnostic_options_.get()),
    diagnostics_engine_(diagnostic_ids_, diagnostic_options_, &diagnostic_consumer_, /*ShouldOwnClient=*/false),
    target_info_(ClangFrontend::create_target_info(diagnostics_engine_, target_options_)), file_manager_(file_system_options_),
    source_manager_(diagnostics_engine_, file_manager_),
    header_search_(header_search_options_, source_manager_, diagnostics_engine_, lang_options_, target_info_.get())
{
}

void ClangFrontend::begin_source_file(SourceFile const& source_file, TranslationUnit& translation_unit)
{
  clang::FileID file_id = source_manager_.createFileID(source_file.get_memory_buffer_ref());
  if (file_id.isInvalid())
    THROW_LALERT("Unable to create FileID for input buffer: [FILENAME]", AIArgs("[FILENAME]", source_file.filename()));
  source_manager_.setMainFileID(file_id);

  auto preprocessor = std::make_unique<clang::Preprocessor>(preprocessor_options_, diagnostics_engine_, lang_options_,
      source_manager_, header_search_, module_loader_, /*IILookup=*/nullptr, /*OwnsHeaderSearch=*/false);

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

#ifdef CWDEBUG
struct PrintSourceLocation
{
  clang::SourceManager const& source_manager_;

  explicit PrintSourceLocation(clang::SourceManager const& source_manager) : source_manager_(source_manager) { }

  std::string operator()(clang::SourceLocation loc) const
  {
    if (loc.isInvalid())
      return "<invalid SourceLocation>";
    if (loc.isMacroID())
    {
      return "<macro>" + loc.printToString(source_manager_) + "</macro>";
    }
    std::pair<clang::FileID, unsigned int> location = source_manager_.getDecomposedLoc(loc);
    unsigned int line = source_manager_.getLineNumber(location.first, location.second);
    unsigned int column = source_manager_.getColumnNumber(location.first, location.second);
    return std::to_string(line) + ":" + std::to_string(column);
  }
};
#endif

void ClangFrontend::process_input_buffer(SourceFile const& source_file, TranslationUnit& translation_unit) const
{
  clang::Preprocessor& pp = translation_unit.get_pp();

  // --- 3. Attach Callbacks ---
  std::vector<PreprocessorEvent> pp_events;
  auto callback_recorder = std::make_unique<MacroCallbackRecorder>(pp, pp_events);
  pp.addPPCallbacks(std::move(callback_recorder));

  // --- 4. Initialize Preprocessor & Configure ---
  pp.Initialize(*target_info_);
  pp.SetCommentRetentionState(true, true);
  pp.SetSuppressIncludeNotFoundError(true);

  // --- 5. Enter Main File ---
  pp.EnterMainSourceFile();

  // --- 6. The Enhanced Tokenization Loop ---
  Dout(dc::notice, "--- Tokens and Whitespace for " << source_file.filename() << " ---");
  clang::Token tok;

  clang::SourceLocation FileStartLoc = source_manager_.getLocForStartOfFile(translation_unit.file_id());
  unsigned LastOffset = 0; // Track the offset *after* the last processed entity *in this file*

#ifdef CWDEBUG
  PrintSourceLocation print_source_location(source_manager_);
#endif

  for (;;)
  {
    // --- Lex the next token ---
    pp.Lex(tok);

    // Stop if we reached the end of the file.
    if (tok.is(clang::tok::eof))
      break;

    clang::SourceLocation CurrentLoc = tok.getLocation();
    Dout(dc::notice, "CurrentLoc = " << print_source_location(CurrentLoc));

    // --- Check if the token's EXPANSION location is in our main file ---
    if (!source_manager_.isInFileID(CurrentLoc, translation_unit.file_id()))
    {
      // This token's expansion location is not in the main file
      // (e.g., maybe from a builtin if UsePredefines was true, or other complex cases).
      // Print its info but don't use it for gap calculation in *this* file.
      clang::SourceLocation SpellingLoc = source_manager_.getSpellingLoc(CurrentLoc);
      char const* tokenName = clang::tok::getTokenName(tok.getKind());
      std::string spelling = pp.getSpelling(tok);

      Dout(dc::notice, "Token(External): "
             << ", Kind: " << tokenName << " (" << tok.getKind() << ")"
             << ", Text: '" << /* escaping needed */ spelling << "'");
      clang::SourceLocation ExpansionLoc = source_manager_.getExpansionLoc(CurrentLoc);
      Dout(dc::notice, "ExpansionLoc = " << print_source_location(ExpansionLoc));
      Dout(dc::notice, "SpellingLoc = " << print_source_location(SpellingLoc));
      if (!source_manager_.isInFileID(SpellingLoc, translation_unit.file_id()))
      {
        clang::SourceLocation SpellingLoc2 = source_manager_.getSpellingLoc(SpellingLoc);
        Dout(dc::notice, "SpellingLoc2 = " << print_source_location(SpellingLoc2));
      }

      continue; // Skip gap calculation for this token
    }

    // --- Token is in the main file ---
    unsigned CurrentOffset = source_manager_.getFileOffset(CurrentLoc);

    // --- Calculate Token Length IN THE FILE BUFFER ---
    // Use Lexer::MeasureTokenLength for accuracy at the expansion location
    unsigned LengthInFile = clang::Lexer::MeasureTokenLength(CurrentLoc, source_manager_, lang_options_);
    unsigned CurrentEndOffset = CurrentOffset + LengthInFile;

    // --- WHITESPACE/COMMENT/DIRECTIVE RECOVERY (Gap Calculation) ---
    if (CurrentOffset < LastOffset)
    {
      // This shouldn't happen with correct logic, but indicates an overlap or error
      Dout(dc::notice, "[Warning: Token offset " << CurrentOffset << " is before LastOffset " << LastOffset << ". Skipping gap.]");
      // Avoid calculating a negative gap. Decide how to handle this - maybe reset LastOffset?
      // For now, just skip the gap and process the token.
    }
    else if (CurrentOffset > LastOffset)
    {
      unsigned GapLength = CurrentOffset - LastOffset;
      llvm::StringRef GapText(source_file.begin() + LastOffset, GapLength);

      Dout(dc::notice, "Gap : FileOffset: " << LastOffset << ", Length: " << GapLength << ", Text: '" << buf2str(GapText.data(), GapText.size()) << "'");
    }
    // else CurrentOffset == LastOffset, means no gap, which is normal.

    // --- PROCESS THE REGULAR TOKEN (that is in the main file) ---
    unsigned ExpLine = source_manager_.getExpansionLineNumber(CurrentLoc);  // Line in the file
    unsigned ExpCol = source_manager_.getExpansionColumnNumber(CurrentLoc); // Column in the file
    clang::SourceLocation SpellingLoc = source_manager_.getSpellingLoc(CurrentLoc);

    clang::tok::TokenKind kind = tok.getKind();
    char const* tokenName = clang::tok::getTokenName(kind);
    std::string spelling = pp.getSpelling(tok);                           // Spelling (might differ from file text)

    Dout(dc::notice, "Token: Line: " << ExpLine << ", Col: " << ExpCol    // Expansion/File Location
           << " (Spelling: " << print_source_location(SpellingLoc) << ")" // Spelling Location
           << ", Kind: " << tokenName << " (" << kind << ")"
           << ", FileOffset: " << CurrentOffset << ", LengthInFile: " << LengthInFile << ", Text: '" <<
           buf2str(spelling.data(), spelling.size()) << "'");

    // --- Update LastOffset to the position *after* this token IN THE FILE ---
    LastOffset = CurrentEndOffset;

    // --- Add the token to the translation unit ---
    translation_unit.add_input_token(tok, CurrentOffset, LengthInFile, ExpLine, ExpCol);
  }

  // --- Process any remaining gap at the end of the file ---
  unsigned FileEndOffset = source_file.size();
  if (FileEndOffset > LastOffset)
  {
    unsigned GapLength = FileEndOffset - LastOffset;
    llvm::StringRef GapText(source_file.begin() + LastOffset, GapLength);
    Dout(dc::notice, "End of File Gap: FileOffset: " << LastOffset << ", Length: " << GapLength << ", Text: '" << buf2str(GapText.data(), GapText.size()) << "'");
  }

  Dout(dc::notice, "--- End of Tokens and Whitespace ---");
  Dout(dc::notice, "--- Preprocessor Events ---");
  // ... (Output pp_events as before) ...
  for (auto const& event : pp_events)
  {
    // Use Expansion loc for line/col as it's often more relevant for where it occurred.
    unsigned eventLine = source_manager_.getExpansionLineNumber(event.Location.getBegin());
    unsigned eventCol = source_manager_.getExpansionColumnNumber(event.Location.getBegin());
    std::string typeStr = (event.Type == PreprocessorEvent::MACRO_DEFINITION) ? "DEFINITION" : "EXPANSION";
    Dout(dc::notice, "Event: Type: " << typeStr << ", Name: '" << event.Name << "'" << ", Line: " << eventLine << ", Col: " << eventCol);
  }
  Dout(dc::notice, "--- End of Preprocessor Events ---");
}
