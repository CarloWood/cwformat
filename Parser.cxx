#include "sys.h"
#include "clang/Lex/Preprocessor.h"
#include "Parser.h"
#include "utils/AIAlert.h"

Parser::Parser() :
    diagnostic_ids_(new clang::DiagnosticIDs),
    diagnostic_consumer_(llvm::errs(), diagnostic_options_.get()),
    diagnostics_engine_(diagnostic_ids_, diagnostic_options_, &diagnostic_consumer_, /*ShouldOwnClient=*/false),
    target_info_(Parser::create_target_info(diagnostics_engine_, target_options_)), file_manager_(file_system_options_),
    source_manager_(diagnostics_engine_, file_manager_),
    header_search_(header_search_options_, source_manager_, diagnostics_engine_, lang_options_, target_info_.get())
{
}

//static
clang::TargetInfo* Parser::create_target_info(
  clang::DiagnosticsEngine& diagnostics_engine, std::shared_ptr<clang::TargetOptions> const& target_options)
{
  clang::TargetInfo* target_info = clang::TargetInfo::CreateTargetInfo(diagnostics_engine, target_options);
  if (!target_info)
    THROW_ALERT("Unable to create target info for triple: [TRIPLE]", AIArgs("[TRIPLE]", target_options->Triple));
  return target_info;
};

void Parser::process_input_buffer(std::string const& input_filename_for_diagnostics,
  std::unique_ptr<llvm::MemoryBuffer> input_buffer, // Takes ownership
  std::ostream& output)
{
  // 1. Register the buffer with SourceManager to get a FileID
  // Note: createFileID takes ownership of the unique_ptr via std::move implicitly
  //       if an rvalue reference overload is matched, or explicitly via std::move.
  //       The signature takes llvm::MemoryBufferRef, so we need to get one.
  // Alternative: Use the overload taking unique_ptr directly if available and preferred.
  // Let's use the unique_ptr overload for clarity since we have one.
  clang::FileID fid = source_manager_.createFileID(std::move(input_buffer));
  // input_buffer is now null, ownership transferred to SourceManager

  // Tell the SourceManager that 'fid' represents the main source file.
  source_manager_.setMainFileID(fid);

  if (fid.isInvalid())
  {
    // Report error - using llvm::errs() or your logging mechanism
    llvm::errs() << "Error: Could not create FileID for buffer '" << input_filename_for_diagnostics << "'\n";
    // Or report via DiagnosticsEngine if possible/meaningful without a location
    // Or write to the output stream
    output << "Error: Failed to create FileID for input.\n";
    return; // Cannot proceed without a valid FileID
  }

  // --- Diagnostics specific to this buffer ---
  // You might want to clear any previous errors if the DiagnosticsEngine is reused heavily,
  // though typically errors are associated with SourceLocations within a specific FileID.
  // diagnostics_engine_.Reset(); // Use cautiously if needed.

  // 2. Create a *new* Preprocessor instance for this buffer
  //    It uses the shared/reusable components from the Parser members.
  clang::Preprocessor pp(preprocessor_options_, // Shared options
    diagnostics_engine_,                        // Shared engine
    lang_options_,                              // Shared LangOpts
    source_manager_,                            // Shared SourceManager
    header_search_,                             // Shared HeaderSearch
    module_loader_,                             // Shared ModuleLoader
    /*IdentifierInfoLookup=*/nullptr,           // Usually not needed for lexing only.
    /*OwnsHeaderSearch=*/false                  // We own header_search_ in Parser.
    /*TUKind = TU_Complete*/);

  // 3. Configure the Preprocessor instance.
  pp.SetCommentRetentionState(/*KeepComments=*/true, /*KeepMacroComments=*/true); // Crucial for getting comments as tokens.

  // Optional: Further configure pp if needed (e.g., suppress include errors if you
  // lex code that might have invalid #includes you want to ignore).
  pp.SetSuppressIncludeNotFoundError(true);

  // 4. Enter the source file into the Preprocessor.
  // This sets up the lexer to start reading from the beginning of our FileID.
  pp.EnterMainSourceFile(); // Use this when processing a single buffer as the main entry

  // 5. The Tokenization Loop.
  clang::Token tok;
  output << "--- Tokens for " << input_filename_for_diagnostics << " ---\n";

  do
  {
    // Fetch the next token from the buffer via the Preprocessor.
    pp.Lex(tok);

    // Check for lexing errors reported *during* pp.Lex().
    if (diagnostics_engine_.hasErrorOccurred())
    {
      output << "[Lexing Error Detected during token fetch]\n";
      // You might want to stop, or clear the error and try to continue
      // diagnostics_engine_.Reset(); // Reset error state if continuing
      // break; // Or simply stop processing this buffer on error
    }

    // Exit loop cleanly on End-Of-File token.
    if (tok.is(clang::tok::eof))
    {
      break;
    }

    // 6. Process the current Token 'tok'

    clang::SourceLocation loc = tok.getLocation();

    // A token MUST have a valid location unless it's EOF or an annotation token
    if (loc.isInvalid())
    {
      // This might happen for special tokens (e.g., annotations), skip them
      output << "[Skipping token with invalid location: Kind " << tok.getKind() << "]\n";
      continue;
    }

    // Get location details (maps back to the original buffer)
    // Use getSpellingLineNumber/ColumnNumber for locations in the original file
    unsigned line = source_manager_.getSpellingLineNumber(loc);
    unsigned col = source_manager_.getSpellingColumnNumber(loc);

    // Get token kind and name
    clang::tok::TokenKind kind = tok.getKind();
    const char* tokenName = clang::tok::getTokenName(kind);

    // Get token length in the source buffer
    unsigned tokLen = tok.getLength();

    // Get the actual text (spelling) of the token
    // Using pp.getSpelling() is generally robust as it handles trigraphs,
    // string literal concatenation nuances, etc.
    std::string spelling = pp.getSpelling(tok);

    // --- Output the token information to the provided ostream ---
    output << "Line: " << line << ", Col: " << col << ", Kind: " << tokenName << " (" << kind << ")"
           << ", Length: " << tokLen << ", Text: '";

    // Escape special characters in the spelling for clearer output
    for (char c : spelling)
    {
      switch (c)
      {
        case '\n':
          output << "\\n";
          break;
        case '\t':
          output << "\\t";
          break;
        case '\r':
          output << "\\r";
          break;
        case '\\':
          output << "\\\\";
          break; // Escape backslash itself
        case '\'':
          output << "\\'";
          break; // Escape single quote
        default:
          // Output printable characters directly
          // Use isprint from <cctype>
          if (std::isprint(static_cast<unsigned char>(c)))
          {
            output << c;
          }
          else
          {
            // Represent non-printable characters using hex escape codes
            // Use llvm::format_hex_no_prefix from <llvm/Support/Format.h>
            output << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
          }
          break;     // Need break here
      }
    }
    output << "'\n"; // Close the single quote and add newline

  } while (tok.isNot(clang::tok::eof)); // Continue until EOF is reached

  output << "--- End of Tokens for " << input_filename_for_diagnostics << " ---\n";

  // Optional: You might clear the FileID entries from the SourceManager
  // if you are processing a huge number of files and memory is a concern,
  // but only do this if you are sure you won't need any SourceLocations
  // referring to this buffer later (e.g., for diagnostics reporting outside this loop).
  // source_manager_.ClearFileEntries(fid); // Use with care!
}
