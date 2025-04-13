#pragma once

#include <clang/Basic/SourceManager.h>
#include <clang/Lex/MacroArgs.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/Support/raw_ostream.h> // For printing debug info if needed
#include <ostream>                    // For outputting results
#include <string>
#include <vector>
#ifdef CWDEBUG
#include "debug_ostream_operators.h"
#endif
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

class PreprocessorEventsHandler : public clang::PPCallbacks
{
 private:
  TranslationUnit const& translation_unit_;
  std::vector<PreprocessorEvent>& EventsOutput; // Store events here.
#ifdef CWDEBUG
  auto print_source_location(clang::SourceLocation loc) { return PrintSourceLocation{translation_unit_}(loc); }
  auto print_source_range(clang::SourceRange const& range) { return PrintSourceRange{translation_unit_}(range); }
  auto print_char_source_range(clang::CharSourceRange const& char_range) { return PrintCharSourceRange{translation_unit_}(char_range); }
  auto print_token(clang::Token const& token) { return PrintToken{translation_unit_}(token); }
#endif

 public:
  PreprocessorEventsHandler(clang::SourceManager& source_manager, TranslationUnit const& translation_unit,
    std::vector<PreprocessorEvent>& EventsOutputRef) : translation_unit_(translation_unit), EventsOutput(EventsOutputRef)
  {
  }

  void InclusionDirective(clang::SourceLocation HashLoc, clang::Token const& IncludeTok, clang::StringRef FileName, bool IsAngled,
    clang::CharSourceRange FilenameRange, clang::OptionalFileEntryRef File, clang::StringRef SearchPath, clang::StringRef RelativePath,
    clang::Module const* SuggestedModule, bool ModuleImported, clang::SrcMgr::CharacteristicKind FileType) override
  {
    DoutEntering(dc::notice,
      "PreprocessorEventsHandler::InclusionDirective(" << print_source_location(HashLoc) << ", " << print_token(IncludeTok) << ", " << FileName
                                                       << ", " << std::boolalpha << IsAngled << ", " << print_char_source_range(FilenameRange)
                                                       << ", " << File << ", " << SearchPath << ", " << RelativePath << ", " << SuggestedModule
                                                       << ", " << std::boolalpha << ModuleImported << ", " << FileType << ")");
  }

  // Called when a macro is defined (#define).
  void MacroDefined(clang::Token const& MacroNameTok, clang::MacroDirective const* MD) override
  {
    if (!MD || !MD->getMacroInfo())
      return; // Basic sanity check.

    clang::SourceLocation begin = MD->getMacroInfo()->getDefinitionLoc();
    clang::SourceLocation end = MD->getMacroInfo()->getDefinitionEndLoc();
    clang::SourceRange DefRange(begin, end);

    EventsOutput.emplace_back(PreprocessorEvent::MACRO_DEFINITION, MacroNameTok.getIdentifierInfo()->getName().str(), DefRange);
    Dout(dc::notice, "Callback: Defined macro '" << print_token(MacroNameTok) << "' at " << print_source_range(DefRange));
  }

  // Called when a macro is invoked and about to be expanded.
  void MacroExpands(
    clang::Token const& MacroNameTok, clang::MacroDefinition const& MD, clang::SourceRange source_range, clang::MacroArgs const* Args) override
  {
    // source_range refers to the macro invocation in the source file.
    EventsOutput.emplace_back(PreprocessorEvent::MACRO_EXPANSION, MacroNameTok.getIdentifierInfo()->getName().str(), source_range);
    Dout(dc::notice, "Callback: Expanding macro '" << print_token(MacroNameTok) << "' at " << print_source_range(source_range));
  }

  // Override other PPCallbacks methods if needed (e.g., Ifdef, Ifndef, Include...)
};
