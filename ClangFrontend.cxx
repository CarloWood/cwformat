#include "sys.h"
#include "ClangFrontend.h"
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

//static
clang::TargetInfo* ClangFrontend::create_target_info(
  clang::DiagnosticsEngine& diagnostics_engine, std::shared_ptr<clang::TargetOptions> const& target_options)
{
  clang::TargetInfo* target_info = clang::TargetInfo::CreateTargetInfo(diagnostics_engine, target_options);
  if (!target_info)
    THROW_LALERT("Unable to create target info for triple: [TRIPLE]", AIArgs("[TRIPLE]", target_options->Triple));
  return target_info;
};

void ClangFrontend::process_input_buffer(
  std::string const& input_filename_for_diagnostics, std::unique_ptr<llvm::MemoryBuffer> input_buffer, std::ostream& output)
{
  // --- 1. Setup FileID and SourceManager ---
  llvm::MemoryBuffer const* input_buffer_ptr = input_buffer.get();
  clang::FileID fid = source_manager_.createFileID(std::move(input_buffer));
  if (fid.isInvalid())
    THROW_LALERT("Unable to create FileID for input buffer: [FILENAME]", AIArgs("[FILENAME]", input_filename_for_diagnostics));
  source_manager_.setMainFileID(fid);

  // --- 2. Create Preprocessor ---
  clang::Preprocessor pp(preprocessor_options_, diagnostics_engine_, lang_options_, source_manager_, header_search_, module_loader_,
    /*IILookup=*/nullptr, /*OwnsHeaderSearch=*/false);

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
  Dout(dc::notice, "--- Tokens and Whitespace for " << input_filename_for_diagnostics << " ---");
  clang::Token tok;

  clang::SourceLocation FileStartLoc = source_manager_.getLocForStartOfFile(fid);
  unsigned LastOffset = 0; // Track the offset *after* the last processed entity *in this file*

  do
  {
    pp.Lex(tok);

    if (tok.is(clang::tok::eof))
    {
      break;
    }

    clang::SourceLocation CurrentLoc = tok.getLocation();
    if (CurrentLoc.isInvalid())
    {
      Dout(dc::notice, "[Skipping token with invalid location: Kind " << tok.getKind() << "]");
      continue;
    }

    // --- Check if the token's EXPANSION location is in our main file ---
    if (!source_manager_.isInFileID(CurrentLoc, fid))
    {
      // This token's expansion location is not in the main file
      // (e.g., maybe from a builtin if UsePredefines was true, or other complex cases).
      // Print its info but don't use it for gap calculation in *this* file.
      clang::SourceLocation SpellingLoc = source_manager_.getSpellingLoc(CurrentLoc);
      unsigned SpellingLine = source_manager_.getSpellingLineNumber(SpellingLoc);
      unsigned SpellingCol = source_manager_.getSpellingColumnNumber(SpellingLoc);
      char const* tokenName = clang::tok::getTokenName(tok.getKind());
      std::string spelling = pp.getSpelling(tok);

      Dout(dc::notice, "Token(External): "
             << " (Spelling: " << SpellingLine << ":" << SpellingCol << ")"
             << ", Kind: " << tokenName << " (" << tok.getKind() << ")"
             << ", Text: '" << /* escaping needed */ spelling << "'");
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
      llvm::StringRef GapText(input_buffer_ptr->getBufferStart() + LastOffset, GapLength);

      Dout(dc::notice, "Gap : FileOffset: " << LastOffset << ", Length: " << GapLength << ", Text: '" << buf2str(GapText.data(), GapText.size()) << "'");
    }
    // else CurrentOffset == LastOffset, means no gap, which is normal.

    // --- PROCESS THE REGULAR TOKEN (that is in the main file) ---
    unsigned ExpLine = source_manager_.getExpansionLineNumber(CurrentLoc);  // Line in the file
    unsigned ExpCol = source_manager_.getExpansionColumnNumber(CurrentLoc); // Column in the file
    clang::SourceLocation SpellingLoc = source_manager_.getSpellingLoc(CurrentLoc);
    unsigned SpellingLine = source_manager_.getSpellingLineNumber(SpellingLoc);
    unsigned SpellingCol = source_manager_.getSpellingColumnNumber(SpellingLoc);

    clang::tok::TokenKind kind = tok.getKind();
    char const* tokenName = clang::tok::getTokenName(kind);
    std::string spelling = pp.getSpelling(tok);                           // Spelling (might differ from file text)

    Dout(dc::notice, "Token: Line: " << ExpLine << ", Col: " << ExpCol    // Expansion/File Location
           << " (Spelling: " << SpellingLine << ":" << SpellingCol << ")" // Spelling Location
           << ", Kind: " << tokenName << " (" << kind << ")"
           << ", FileOffset: " << CurrentOffset << ", LengthInFile: " << LengthInFile << ", Text: '" <<
           buf2str(spelling.data(), spelling.size()) << "'");

    // --- Update LastOffset to the position *after* this token IN THE FILE ---
    LastOffset = CurrentEndOffset;

  } while (tok.isNot(clang::tok::eof));

  // --- Process any remaining gap at the end of the file ---
  unsigned FileEndOffset = input_buffer_ptr->getBufferSize();
  if (FileEndOffset > LastOffset)
  {
    unsigned GapLength = FileEndOffset - LastOffset;
    llvm::StringRef GapText(input_buffer_ptr->getBufferStart() + LastOffset, GapLength);
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
