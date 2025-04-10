#pragma once

#include <clang/Basic/SourceManager.h>
#include <clang/Lex/MacroArgs.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/Support/raw_ostream.h> // For printing debug info if needed
#include <ostream>                    // For outputting results
#include <string>
#include <vector>
#include "debug.h"

// Structure to hold information about preprocessor events we care about
struct PreprocessorEvent
{
  enum EventType
  {
    MACRO_DEFINITION,
    MACRO_EXPANSION
    // Could add others like inclusion directives, skipped ranges etc.
  } Type;

  std::string Name;
  clang::SourceRange Location; // Definition site or Invocation site
  // Optionally store more details like macro body range, arguments, etc.

  // Constructor
  PreprocessorEvent(EventType T, std::string N, clang::SourceRange L) : Type(T), Name(std::move(N)), Location(L) {}
};

class MacroCallbackRecorder : public clang::PPCallbacks
{
 private:
  clang::Preprocessor& PP;                      // Need access to PP for SourceManager etc.
  clang::SourceManager& SM;
  std::vector<PreprocessorEvent>& EventsOutput; // Store events here.

 public:
  MacroCallbackRecorder(clang::Preprocessor& PP, std::vector<PreprocessorEvent>& EventsOutputRef) :
      PP(PP), SM(PP.getSourceManager()), EventsOutput(EventsOutputRef)
  {
  }

  // Called when a macro is defined (#define).
  void MacroDefined(const clang::Token& MacroNameTok, const clang::MacroDirective* MD) override
  {
    if (!MD || !MD->getMacroInfo())
      return; // Basic sanity check.

    clang::SourceLocation begin = MD->getMacroInfo()->getDefinitionLoc();
    clang::SourceLocation end = MD->getMacroInfo()->getDefinitionEndLoc();
    clang::SourceRange DefRange(begin, end);

    EventsOutput.emplace_back(PreprocessorEvent::MACRO_DEFINITION, MacroNameTok.getIdentifierInfo()->getName().str(), DefRange);
    Dout(dc::notice,
      "Callback: Defined macro '" << static_cast<std::string_view>(MacroNameTok.getIdentifierInfo()->getName()) << "' at "
                                  << DefRange.getBegin().printToString(SM) << " - " << DefRange.getEnd().printToString(SM));
  }

  // Called when a macro is invoked and about to be expanded.
  void MacroExpands(
    const clang::Token& MacroNameTok, const clang::MacroDefinition& MD, clang::SourceRange Range, const clang::MacroArgs* Args) override
  {
    // Range is the source range of the macro invocation *in the source file*
    EventsOutput.emplace_back(PreprocessorEvent::MACRO_EXPANSION, MacroNameTok.getIdentifierInfo()->getName().str(), Range);
    Dout(dc::notice,
      "Callback: Expanding macro '" << static_cast<std::string_view>(MacroNameTok.getIdentifierInfo()->getName()) << "' at "
                                    << Range.getBegin().printToString(SM) << " - " << Range.getEnd().printToString(SM));
  }

  // Override other PPCallbacks methods if needed (e.g., Ifdef, Ifndef, Include...)
};
