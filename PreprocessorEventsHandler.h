#pragma once

#include "TranslationUnitRef.h"
#include "InputToken.h"
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

class PreprocessorEventsHandler : public clang::PPCallbacks, public TranslationUnitRef
{
 public:
  PreprocessorEventsHandler(clang::SourceManager& source_manager, TranslationUnit& translation_unit) : TranslationUnitRef(translation_unit) { }

  void InclusionDirective(clang::SourceLocation HashLoc, clang::Token const& IncludeTok, clang::StringRef FileName, bool IsAngled,
    clang::CharSourceRange FilenameRange, clang::OptionalFileEntryRef File, clang::StringRef SearchPath, clang::StringRef RelativePath,
    clang::Module const* SuggestedModule, bool ModuleImported, clang::SrcMgr::CharacteristicKind FileType) override
  {
    DoutEntering(dc::notice,
      "PreprocessorEventsHandler::InclusionDirective(" << print_source_location(HashLoc) << ", " << print_token(IncludeTok) << ", " << FileName
                                                       << ", " << std::boolalpha << IsAngled << ", " << print_char_source_range(FilenameRange)
                                                       << ", " << File << ", " << SearchPath << ", " << RelativePath << ", " << SuggestedModule
                                                       << ", " << std::boolalpha << ModuleImported << ", " << FileType << ")");

    translation_unit_.add_input_token<PPToken>(HashLoc, {PPToken::directive_hash});
    translation_unit_.add_input_token<PPToken>(IncludeTok.getLocation(), {PPToken::directive});

    clang::SourceManager const& source_manager = translation_unit_.source_manager();
    unsigned int begin_offset = source_manager.getFileOffset(FilenameRange.getBegin());
    unsigned int end_offset = source_manager.getFileOffset(FilenameRange.getEnd());
    translation_unit_.add_input_token<PPToken>(begin_offset, end_offset - begin_offset, {PPToken::header_name});
  }

  // Called when a macro is defined (#define).
  void MacroDefined(clang::Token const& MacroNameTok, clang::MacroDirective const* MD) override
  {
    DoutEntering(dc::notice,
        "PreprocessorEventsHandler::MacroDefined(" << print_token(MacroNameTok) << ", " << print_macro_directive(*MD) << ")");

    clang::SourceLocation begin = MD->getMacroInfo()->getDefinitionLoc();
    clang::SourceLocation end = MD->getMacroInfo()->getDefinitionEndLoc();
    clang::SourceRange DefRange(begin, end);
  }

  // Called when a macro is invoked and about to be expanded.
  void MacroExpands(
    clang::Token const& MacroNameTok, clang::MacroDefinition const& MD, clang::SourceRange source_range, clang::MacroArgs const* Args) override
  {
    // source_range refers to the macro invocation in the source file.
    Dout(dc::notice, "Callback: Expanding macro '" << print_token(MacroNameTok) << "' at " << print_source_range(source_range));
  }

  // Override other PPCallbacks methods if needed (e.g., Ifdef, Ifndef, Include...)
};
