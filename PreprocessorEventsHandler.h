#pragma once

#include "TranslationUnitRef.h"
#include "InputToken.h"
#include "TranslationUnit.h"
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

    // I'd say this is trivially the case, for a filename.
    ASSERT(FilenameRange.isCharRange());

    // Add the directive hash.
    translation_unit_.add_input_token<PPToken>(HashLoc, {PPToken::directive_hash});
    // Add the include directive (including possible backslash-newlines in the middle).
    translation_unit_.add_input_token<PPToken>(IncludeTok.getLocation(), {PPToken::directive});
    // Add the header name directive; this includes the angle brackets or double quotes, as well as any backslash-newlines.
    translation_unit_.add_input_token<PPToken>(FilenameRange.getAsRange(), {PPToken::header_name});
  }

  // Called when a macro is defined (#define).
  void MacroDefined(clang::Token const& MacroNameTok, clang::MacroDirective const* MD) override
  {
    DoutEntering(dc::notice,
        "PreprocessorEventsHandler::MacroDefined(" << print_token(MacroNameTok) << ", " << print_macro_directive(*MD) << ")");

    using offset_type = TranslationUnit::offset_type;

    // Get the position where the macroname begins:
    //   #  define  macroname ( arg1,  arg2, ...)
    //              ^
    offset_type macro_name_offset = translation_unit_.source_manager().getFileOffset(MacroNameTok.getLocation());
    // Get the position of the directive hash character:
    //   #  define  macroname ( arg1,  arg2, ...)
    //   ^ (has_length will be set to 1)
    auto [hash_offset, hash_length] = translation_unit_.process_gap(macro_name_offset, "#");
    // Add the directive hash.
    translation_unit_.add_input_token<PPToken>(hash_offset, hash_length, {PPToken::directive_hash});

    // Get the position of the `define` keyword:
    //   # def\
    //   ine  macroname
    //     ^ (define_directive_length might be set to a value larger than 6 if there are backslash-newlines in the middle)
    auto [define_directive_offset, define_directive_length] = translation_unit_.process_gap(macro_name_offset, "define");
    // Add the define directive.
    translation_unit_.add_input_token<PPToken>(define_directive_offset, define_directive_length, {PPToken::directive});

    // Get the data related to this macro definition.
    clang::MacroInfo const* macro_info = MD->getMacroInfo();

    // Add the macro name.
    bool is_function_like = macro_info->isFunctionLike();
    translation_unit_.add_input_token<PPToken>(MacroNameTok.getLocation(), {is_function_like ? PPToken::function_macro_name : PPToken::macro_name});

    // Get the position of the opening parenthesis if any.
    if (is_function_like)
    {
      // Add the opening parenthesis. This must immediately follow the macro name.
      translation_unit_.append_input_token(1, {PPToken::function_macro_lparen});
      offset_type const end_offset = translation_unit_.source_manager().getFileOffset(macro_info->getDefinitionEndLoc());

      unsigned int number_of_parameters = macro_info->getNumParams();
      clang::ArrayRef<clang::IdentifierInfo const*> params = macro_info->params();

      for (unsigned int param = 0; param < number_of_parameters; ++param)
      {
        char const* name = params[param]->getNameStart();
        translation_unit_.add_input_token(name, {PPToken::function_macro_param});

        // Not the last parameter?
        if (param < number_of_parameters - 1)
          translation_unit_.add_input_token(",", {PPToken::function_macro_comma});
      }
      // There must be a closing parenthesis.
      translation_unit_.add_input_token(")", {PPToken::function_macro_rparen});
    }

    // Add all replacement tokens.
    for (clang::MacroInfo::const_tokens_iterator token = macro_info->tokens_begin(); token != macro_info->tokens_end(); ++token)
      translation_unit_.add_input_token(token->getLocation(), *token);

    //clang::SourceRange macro_name{MD->getMacroInfo()->getDefinitionLoc(), MD->getMacroInfo()->getDefinitionEndLoc()};
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
