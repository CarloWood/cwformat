#pragma once

#include <clang/Basic/SourceManager.h>
#include <clang/Lex/MacroArgs.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <llvm/Support/raw_ostream.h> // For printing debug info if needed
#include <ostream>                    // For outputting results
#include <string>
#include <vector>
#include "InputToken.h"
#include "TranslationUnit.h"
#include "TranslationUnitRef.h"
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
 private:
  bool enabled_;        // True if the callbacks are enabled. If false, all callbacks should be ignored.

 public:
  using CharacteristicKind = clang::SrcMgr::CharacteristicKind;
  using CharSourceRange = clang::CharSourceRange;
  using FileEntryRef = clang::FileEntryRef;
  using FileID = clang::FileID;
  using IdentifierInfo = clang::IdentifierInfo;
  using LexEmbedParametersResult = clang::LexEmbedParametersResult;
  using MacroDefinition = clang::MacroDefinition;
  using MacroDirective = clang::MacroDirective;
  using Module = clang::Module;
  using ModuleIdPath = clang::ModuleIdPath;
  using OptionalFileEntryRef = clang::OptionalFileEntryRef;
  using PragmaIntroducerKind = clang::PragmaIntroducerKind;
  using SourceLocation = clang::SourceLocation;
  using SourceManager = clang::SourceManager;
  using SourceRange = clang::SourceRange;
  using StringRef = clang::StringRef;
  using Token = clang::Token;

  PreprocessorEventsHandler(TranslationUnit& translation_unit) : TranslationUnitRef(translation_unit), enabled_(true) {}

 private:
  /// Callback invoked whenever an inclusion directive of
  /// any kind (\c \#include, \c \#import, etc.) has been processed, regardless
  /// of whether the inclusion will actually result in an inclusion.
  ///
  /// \param HashLoc The location of the '#' that starts the inclusion
  /// directive.
  ///
  /// \param IncludeTok The token that indicates the kind of inclusion
  /// directive, e.g., 'include' or 'import'.
  ///
  /// \param FileName The name of the file being included, as written in the
  /// source code.
  ///
  /// \param IsAngled Whether the file name was enclosed in angle brackets;
  /// otherwise, it was enclosed in quotes.
  ///
  /// \param FilenameRange The character range of the quotes or angle brackets
  /// for the written file name.
  ///
  /// \param File The actual file that may be included by this inclusion
  /// directive.
  ///
  /// \param SearchPath Contains the search path which was used to find the file
  /// in the file system. If the file was found via an absolute include path,
  /// SearchPath will be empty. For framework includes, the SearchPath and
  /// RelativePath will be split up. For example, if an include of "Some/Some.h"
  /// is found via the framework path
  /// "path/to/Frameworks/Some.framework/Headers/Some.h", SearchPath will be
  /// "path/to/Frameworks/Some.framework/Headers" and RelativePath will be
  /// "Some.h".
  ///
  /// \param RelativePath The path relative to SearchPath, at which the include
  /// file was found. This is equal to FileName except for framework includes.
  ///
  /// \param SuggestedModule The module suggested for this header, if any.
  ///
  /// \param ModuleImported Whether this include was translated into import of
  /// \p SuggestedModule.
  ///
  /// \param FileType The characteristic kind, indicates whether a file or
  /// directory holds normal user code, system code, or system code which is
  /// implicitly 'extern "C"' in C++ mode.
  ///
  void InclusionDirective(SourceLocation HashLoc, Token const& IncludeTok, StringRef FileName, bool IsAngled,
    CharSourceRange FilenameRange, OptionalFileEntryRef File, StringRef SearchPath, StringRef RelativePath,
    Module const* SuggestedModule, bool ModuleImported, CharacteristicKind FileType) override
  {
    if (!enabled_)
    {
      // This #include should not be ignored if it is defined in the current TU.
      ASSERT(!translation_unit_.contains(HashLoc));
      return;
    }

    DoutEntering(dc::notice,
      "PreprocessorEventsHandler::InclusionDirective(" << print_source_location(HashLoc) << ", " << print_token(IncludeTok) << ", " << FileName
                                                       << ", " << std::boolalpha << IsAngled << ", " << print_char_source_range(FilenameRange)
                                                       << ", " << File << ", " << SearchPath << ", " << RelativePath << ", " << SuggestedModule
                                                       << ", " << std::boolalpha << ModuleImported << ", " << FileType << ")");

    // I'd say this is trivially the case, for a filename.
    ASSERT(FilenameRange.isCharRange());

    // Add the directive hash.
    translation_unit_.add_input_token(HashLoc, PPToken::directive_hash);
    // Add the include directive (including possible backslash-newlines in the middle).
    translation_unit_.add_input_token(IncludeTok.getLocation(), PPToken::directive);
    // Add the header name directive; this includes the angle brackets or double quotes, as well as any backslash-newlines.
    translation_unit_.add_input_token(FilenameRange, PPToken::header_name);
  }

  /// Hook called whenever a macro definition is seen.
  void MacroDefined(Token const& MacroNameTok, MacroDirective const* MD) override
  {
    if (!enabled_)
    {
      // This macro should not be ignored if it is defined in the current TU.
      ASSERT(!translation_unit_.contains(MacroNameTok.getLocation()));
      return;
    }

    DoutEntering(
      dc::notice, "PreprocessorEventsHandler::MacroDefined(" << print_token(MacroNameTok) << ", " << print_macro_directive(*MD) << ")");

    // Get the data related to this macro definition.
    clang::MacroInfo const* macro_info = MD->getMacroInfo();
    SourceLocation macro_name_token_location = MacroNameTok.getLocation();
    clang::SourceManager const& source_manager = translation_unit_.clang_frontend().source_manager();

    Debug(
      SourceLocation last_token_location = macro_info->getDefinitionEndLoc();
      clang::CharSourceRange Range = clang::CharSourceRange::getTokenRange(macro_name_token_location, last_token_location);
      llvm::StringRef Text = translation_unit_.clang_frontend().get_source_text(Range);
      Dout(dc::notice, "Macro declaration: '#define " << Text.str() << "'");
    );

    using offset_type = TranslationUnit::offset_type;

    // Get the position where the macroname begins:
    //   #  define  macroname ( arg1,  arg2, ...)
    //              ^
    offset_type macro_name_offset = source_manager.getFileOffset(macro_name_token_location);
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

    // Add the macro name.
    bool is_function_like = macro_info->isFunctionLike();
    translation_unit_.add_input_token(
      macro_name_token_location, is_function_like ? PPToken::function_macro_name : PPToken::macro_name);

    // Get the position of the opening parenthesis if any.
    if (is_function_like)
    {
      // Add the opening parenthesis. This must immediately follow the macro name.
      translation_unit_.append_input_token(1, {PPToken::function_macro_lparen});
      offset_type const end_offset = source_manager.getFileOffset(macro_info->getDefinitionEndLoc());

      unsigned int number_of_parameters = macro_info->getNumParams();
      clang::ArrayRef<clang::IdentifierInfo const*> params = macro_info->params();

      for (unsigned int param = 0; param < number_of_parameters; ++param)
      {
        if (param > 0)
          translation_unit_.add_input_token(",", {PPToken::function_macro_comma});

        // If this is a C99 varargs macro, then the last argument has the `name` "__VAR_ARGS__".
        if (param < number_of_parameters - 1 || !macro_info->isC99Varargs())
        {
          char const* name = params[param]->getNameStart();
          translation_unit_.add_input_token(name, {PPToken::function_macro_param}); // TranslationUnit
        }

        // This is the last parameter?
        if (param == number_of_parameters - 1 && macro_info->isVariadic())
        {
          // If macro_info->isGNUVarargs() is true then the last argument is directly followed by
          // an ellipsis (without a comma), possibly separated by whitespace, e.g.
          //
          //   #define MyMacro(arg1, args ...)
          //                         ^
          //                       name
          //
          // and we have to add the ellipsis now, after the name that was just added.
          //
          // Otherwise this is a C99 variadic macro, where there must be a comma before the
          // ellipsis and the last argument is just that ellipsis. Adding `name` was skipped
          // because that equals the string "__VA_ARGS__".
          //
          //  #define MyMacro(arg1, arg2, ...)
          //
          // In this case we now have to add "..." instead of the name.
          //
          // Therefore, in both cases, add an ellipsis.
          translation_unit_.add_input_token("...", {PPToken::function_macro_ellipsis});
        }
      }
      // There must be a closing parenthesis.
      translation_unit_.add_input_token(")", {PPToken::function_macro_rparen});
    }

    // Add all replacement tokens.
    for (clang::MacroInfo::const_tokens_iterator token = macro_info->tokens_begin(); token != macro_info->tokens_end(); ++token)
      translation_unit_.add_input_token(*token);
  }

  /// Callback invoked whenever the \p Lexer moves to a different file for
  /// lexing. Unlike \p FileChanged line number directives and other related
  /// pragmas do not trigger callbacks to \p LexedFileChanged.
  ///
  /// \param FID The \p FileID that the \p Lexer moved to.
  ///
  /// \param Reason Whether the \p Lexer entered a new file or exited one.
  ///
  /// \param FileType The \p CharacteristicKind of the file the \p Lexer moved
  /// to.
  ///
  /// \param PrevFID The \p FileID the \p Lexer was using before the change.
  ///
  /// \param Loc The location where the \p Lexer entered a new file from or the
  /// location that the \p Lexer moved into after exiting a file.
  void LexedFileChanged(
    FileID FID, LexedFileChangeReason Reason, CharacteristicKind FileType, FileID PrevFID, SourceLocation Loc) override
  {
    DoutEntering(dc::notice, "PreprocessorEventsHandler::LexedFileChanged(" <<
        print_file_id(FID) << ", " << Reason << ", " << FileType << ", " << print_file_id(PrevFID) << ", " << print_source_location(Loc) << ")");

    ASSERT(Reason == LexedFileChangeReason::EnterFile || Reason == LexedFileChangeReason::ExitFile);    // When does this happen?
    // Disable certain functionality if we're not in the current TU.
    enabled_ = FID == translation_unit_.file_id();
  }

  /// Callback invoked whenever a source file is skipped as the result
  /// of header guard optimization.
  ///
  /// \param SkippedFile The file that is skipped instead of entering \#include
  ///
  /// \param FilenameTok The file name token in \#include "FileName" directive
  /// or macro expanded file name token from \#include MACRO(PARAMS) directive.
  /// Note that FilenameTok contains corresponding quotes/angles symbols.
  void FileSkipped(FileEntryRef const& SkippedFile, Token const& FilenameTok, CharacteristicKind FileType) override
  {
    DoutEntering(dc::notice, "PreprocessorEventsHandler::FileSkipped(" <<
        SkippedFile.getName() << ", " << print_token(FilenameTok) << ", " << FileType);
  }

  /// Callback invoked whenever the preprocessor cannot find a file for an
  /// embed directive.
  ///
  /// \param FileName The name of the file being included, as written in the
  /// source code.
  ///
  /// \returns true to indicate that the preprocessor should skip this file
  /// and not issue any diagnostic.
  bool EmbedFileNotFound(StringRef FileName) override { ASSERT(!enabled_);; return false; }

  /// Callback invoked whenever an embed directive has been processed,
  /// regardless of whether the embed will actually find a file.
  ///
  /// \param HashLoc The location of the '#' that starts the embed directive.
  ///
  /// \param FileName The name of the file being included, as written in the
  /// source code.
  ///
  /// \param IsAngled Whether the file name was enclosed in angle brackets;
  /// otherwise, it was enclosed in quotes.
  ///
  /// \param File The actual file that may be included by this embed directive.
  ///
  /// \param Params The parameters used by the directive.
  void EmbedDirective(
    SourceLocation HashLoc, StringRef FileName, bool IsAngled, OptionalFileEntryRef File, LexEmbedParametersResult const& Params) override
  {
    ASSERT(!enabled_);;
  }

  /// Callback invoked whenever the preprocessor cannot find a file for an
  /// inclusion directive.
  ///
  /// \param FileName The name of the file being included, as written in the
  /// source code.
  ///
  /// \returns true to indicate that the preprocessor should skip this file
  /// and not issue any diagnostic.
  bool FileNotFound(StringRef FileName) override
  {
    DoutEntering(dc::notice, "File not found: " << FileName);

    clang::Preprocessor &PP = translation_unit_.get_pp();
    clang::HeaderSearch &HS = PP.getHeaderSearchInfo();

    llvm::errs() << "--- Header Search Directories ---\n";

    // Using the iterator provided by HeaderSearch
    unsigned index = 0;
    for (const auto& DirLookup : llvm::make_range(HS.search_dir_begin(), HS.search_dir_end())) {
        llvm::errs() << "[" << index++ << "] Path: " << DirLookup.getName() << "\n";
        llvm::errs() << "    Kind: ";
        switch (DirLookup.getDirCharacteristic()) {
            case clang::SrcMgr::C_User:        llvm::errs() << "User"; break;
            case clang::SrcMgr::C_System:      llvm::errs() << "System"; break;
            case clang::SrcMgr::C_ExternCSystem: llvm::errs() << "ExternCSystem"; break;
            case clang::SrcMgr::C_User_ModuleMap: llvm::errs() << "User_ModuleMap"; break; // Should not happen
            case clang::SrcMgr::C_System_ModuleMap: llvm::errs() << "System_ModuleMap"; break; // Should not happen
        }
        llvm::errs() << "\n";
        llvm::errs() << "    Is Framework: " << (DirLookup.isFramework() ? "Yes" : "No") << "\n";
        llvm::errs() << "    Is System Header Dir: " << (DirLookup.isSystemHeaderDirectory() ? "Yes" : "No") << "\n";
        llvm::errs() << "---------------------------------\n";
    }

     // Also print the system root and resource dir Clang is using
    llvm::errs() << "Resource Dir: " << HS.getHeaderSearchOpts().ResourceDir << "\n";
    llvm::errs() << "--- End Header Search Directories ---\n";

    return false;
  }

  /// Callback invoked whenever a submodule was entered.
  ///
  /// \param M The submodule we have entered.
  ///
  /// \param ImportLoc The location of import directive token.
  ///
  /// \param ForPragma If entering from pragma directive.
  ///
  void EnteredSubmodule(Module* M, SourceLocation ImportLoc, bool ForPragma) override { ASSERT(!enabled_);; }

  /// Callback invoked whenever a submodule was left.
  ///
  /// \param M The submodule we have left.
  ///
  /// \param ImportLoc The location of import directive token.
  ///
  /// \param ForPragma If entering from pragma directive.
  ///
  void LeftSubmodule(Module* M, SourceLocation ImportLoc, bool ForPragma) override { ASSERT(!enabled_);; }

  /// Callback invoked whenever there was an explicit module-import
  /// syntax.
  ///
  /// \param ImportLoc The location of import directive token.
  ///
  /// \param Path The identifiers (and their locations) of the module
  /// "path", e.g., "std.vector" would be split into "std" and "vector".
  ///
  /// \param Imported The imported module; can be null if importing failed.
  ///
  void moduleImport(SourceLocation ImportLoc, ModuleIdPath Path, Module const* Imported) override { ASSERT(!enabled_);; }

  /// Callback invoked when the end of the main file is reached.
  ///
  /// No subsequent callbacks will be made.
  void EndOfMainFile() override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#ident or \#sccs directive is read.
  /// \param Loc The location of the directive.
  /// \param str The text of the directive.
  ///
  void Ident(SourceLocation Loc, StringRef str) override { ASSERT(!enabled_);; }

  /// Callback invoked when start reading any pragma directive.
  void PragmaDirective(SourceLocation Loc, PragmaIntroducerKind Introducer) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma comment directive is read.
  void PragmaComment(SourceLocation Loc, IdentifierInfo const* Kind, StringRef Str) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma mark comment is read.
  void PragmaMark(SourceLocation Loc, StringRef Trivia) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma detect_mismatch directive is
  /// read.
  void PragmaDetectMismatch(SourceLocation Loc, StringRef Name, StringRef Value) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma clang __debug directive is read.
  /// \param Loc The location of the debug directive.
  /// \param DebugType The identifier following __debug.
  void PragmaDebug(SourceLocation Loc, StringRef DebugType) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma message directive is read.
  /// \param Loc The location of the message directive.
  /// \param Namespace The namespace of the message directive.
  /// \param Kind The type of the message directive.
  /// \param Str The text of the message directive.
  void PragmaMessage(SourceLocation Loc, StringRef Namespace, PragmaMessageKind Kind, StringRef Str) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma gcc diagnostic push directive
  /// is read.
  void PragmaDiagnosticPush(SourceLocation Loc, StringRef Namespace) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma gcc diagnostic pop directive
  /// is read.
  void PragmaDiagnosticPop(SourceLocation Loc, StringRef Namespace) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma gcc diagnostic directive is read.
  void PragmaDiagnostic(SourceLocation Loc, StringRef Namespace, clang::diag::Severity mapping, StringRef Str) override { ASSERT(!enabled_);; }

  /// Called when an OpenCL extension is either disabled or
  /// enabled with a pragma.
  void PragmaOpenCLExtension(SourceLocation NameLoc, IdentifierInfo const* Name, SourceLocation StateLoc, unsigned State) override
  {
    ASSERT(!enabled_);;
  }

  void PragmaWarning(SourceLocation Loc, PragmaWarningSpecifier WarningSpec, clang::ArrayRef<int> Ids) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma warning(push) directive is read.
  void PragmaWarningPush(SourceLocation Loc, int Level) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma warning(pop) directive is read.
  void PragmaWarningPop(SourceLocation Loc) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma execution_character_set(push) directive
  /// is read.
  void PragmaExecCharsetPush(SourceLocation Loc, StringRef Str) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma execution_character_set(pop) directive
  /// is read.
  void PragmaExecCharsetPop(SourceLocation Loc) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma clang assume_nonnull begin directive
  /// is read.
  void PragmaAssumeNonNullBegin(SourceLocation Loc) override { ASSERT(!enabled_);; }

  /// Callback invoked when a \#pragma clang assume_nonnull end directive
  /// is read.
  void PragmaAssumeNonNullEnd(SourceLocation Loc) override { ASSERT(!enabled_);; }

  /// Called by Preprocessor::HandleMacroExpandedIdentifier when a
  /// macro invocation is found.
  void MacroExpands(Token const& MacroNameTok, MacroDefinition const& MD, SourceRange Range, clang::MacroArgs const* Args) override
  {
    if (!enabled_)
    {
      // This macro expansion should not be ignored if it is defined in the current TU.
      ASSERT(!translation_unit_.contains(Range.getBegin()));
      return;
    }

    // Get the data related to this macro definition.
    clang::MacroInfo const* macro_info = MD.getMacroInfo();

#ifdef CWDEBUG
    // Range refers to the macro invocation in the source file.
    DoutEntering(dc::notice|continued_cf, "PreprocessorEventsHandler::MacroExpands(" << print_token(MacroNameTok) << ", MD, " << print_source_range(Range));
    for (unsigned int arg = 0; arg < Args->getNumMacroArguments(); ++arg)
    {
      Token arg_token = *Args->getUnexpArgument(arg);
      Dout(dc::continued, ", " << print_token(arg_token) << " (length: " << arg_token.getLength() << ")");
    }
    Dout(dc::finish, ")");
#endif

    // Add the macro name.
    if (!macro_info->isFunctionLike())
    {
      translation_unit_.add_input_token(MacroNameTok.getLocation(), PPToken::macro_invocation_name);
      return;
    }

    translation_unit_.add_input_token(MacroNameTok.getLocation(), PPToken::function_macro_invocation_name);
  }

  /// Hook called whenever a macro \#undef is seen.
  /// \param MacroNameTok The active Token
  /// \param MD A MacroDefinition for the named macro.
  /// \param Undef New MacroDirective if the macro was defined, null otherwise.
  ///
  /// MD is released immediately following this callback.
  void MacroUndefined(Token const& MacroNameTok, MacroDefinition const& MD, MacroDirective const* Undef) override { ASSERT(!enabled_);; }

  /// Hook called whenever the 'defined' operator is seen.
  /// \param MD The MacroDirective if the name was a macro, null otherwise.
  void Defined(Token const& MacroNameTok, MacroDefinition const& MD, SourceRange Range) override { ASSERT(!enabled_);; }

  /// Hook called when a '__has_embed' directive is read.
  void HasEmbed(SourceLocation Loc, StringRef FileName, bool IsAngled, OptionalFileEntryRef File) override { ASSERT(!enabled_);; }

  /// Hook called when a '__has_include' or '__has_include_next' directive is
  /// read.
  void HasInclude(
    SourceLocation Loc, StringRef FileName, bool IsAngled, OptionalFileEntryRef File, CharacteristicKind FileType) override { ASSERT(!enabled_);; }

  /// Hook called when a source range is skipped.
  /// \param Range The SourceRange that was skipped. The range begins at the
  /// \#if/\#else directive and ends after the \#endif/\#else directive.
  /// \param EndifLoc The end location of the 'endif' token, which may precede
  /// the range skipped by the directive (e.g excluding comments after an
  /// 'endif').
  void SourceRangeSkipped(SourceRange Range, SourceLocation EndifLoc) override { ASSERT(!enabled_);; }

  auto print_argument(Token const& token)
  {
    return print_token(token);
  }

  auto print_argument(SourceRange const& source_range)
  {
    return print_source_range(source_range);
  }

  auto print_argument(clang::FileID file_id)
  {
    return print_file_id(file_id);
  }

  auto print_argument(clang::SourceLocation loc)
  {
    return print_source_location(loc);
  }

  auto print_argument(clang::CharSourceRange const& char_range)
  {
    return print_char_source_range(char_range);
  }

  auto print_argument(clang::MacroDirective const& macro_directive)
  {
    return print_macro_directive(macro_directive);
  }

  // Adds PPToken::directive_hash followed by the PPToken::directive at DirectiveLocation.
  template<typename ...Args>
  void add_directive(SourceLocation DirectiveLocation COMMA_CWDEBUG_ONLY(char const* func_name, Args&&... args))
  {
    if (!enabled_)
    {
      // This directive should not be ignored if it is used in the current TU.
      ASSERT(!translation_unit_.contains(DirectiveLocation));
      return;
    }

#ifdef CWDEBUG
    int debug_indentation = 2;
    LibcwDoutScopeBegin(DEBUGCHANNELS, ::libcwd::libcw_do, dc::notice)
    LibcwDoutStream << "Entering PreprocessorEventsHandler::" << func_name << "(" << print_source_location(DirectiveLocation);
    ( (LibcwDoutStream << ", " << print_argument(std::forward<Args>(args))), ... );
    LibcwDoutStream << ")";
    LibcwDoutScopeEnd;
    NAMESPACE_DEBUG::Indent debug_indent(debug_indentation);
#endif

    // Add the directive hash and the directive itself.
    // The very first non-whitespace or comment should be the hash that belongs to the directive.
    translation_unit_.add_input_token("#", PPToken::directive_hash);
    translation_unit_.add_input_token(DirectiveLocation, PPToken::directive);
  }

  /// Hook called whenever an \#if is seen.
  /// \param Loc the source location of the directive.
  /// \param ConditionRange The SourceRange of the expression being tested.
  /// \param ConditionValue The evaluated value of the condition.
  ///
  void If(SourceLocation DirectiveLocation, SourceRange ConditionRange, ConditionValueKind ConditionValue) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("If", ConditionRange));
  }

  /// Hook called whenever an \#elif is seen.
  /// \param Loc the source location of the directive.
  /// \param ConditionRange The SourceRange of the expression being tested.
  /// \param ConditionValue The evaluated value of the condition.
  /// \param IfLoc the source location of the \#if/\#ifdef/\#ifndef directive.
  void Elif(SourceLocation DirectiveLocation, SourceRange ConditionRange, ConditionValueKind ConditionValue, SourceLocation IfLoc) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Elif"));
  }

  /// Hook called whenever an \#ifdef is seen.
  /// \param Loc the source location of the directive.
  /// \param MacroNameTok Information on the token being tested.
  /// \param MD The MacroDefinition if the name was a macro, null otherwise.
  void Ifdef(SourceLocation DirectiveLocation, Token const& MacroNameTok, MacroDefinition const& MD) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Ifdef", MacroNameTok));
  }

  /// Hook called whenever an \#elifdef branch is taken.
  /// \param Loc the source location of the directive.
  /// \param MacroNameTok Information on the token being tested.
  /// \param MD The MacroDefinition if the name was a macro, null otherwise.
  void Elifdef(SourceLocation DirectiveLocation, Token const& MacroNameTok, MacroDefinition const& MD) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Elifdef"));
  }

  /// Hook called whenever an \#elifdef is skipped.
  /// \param Loc the source location of the directive.
  /// \param ConditionRange The SourceRange of the expression being tested.
  /// \param IfLoc the source location of the \#if/\#ifdef/\#ifndef directive.
  void Elifdef(SourceLocation DirectiveLocation, SourceRange ConditionRange, SourceLocation IfLoc) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Elifdef"));
  }

  /// Hook called whenever an \#ifndef is seen.
  /// \param Loc the source location of the directive.
  /// \param MacroNameTok Information on the token being tested.
  /// \param MD The MacroDefiniton if the name was a macro, null otherwise.
  void Ifndef(SourceLocation DirectiveLocation, Token const& MacroNameTok, MacroDefinition const& MD) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Ifndef"));
  }

  /// Hook called whenever an \#elifndef branch is taken.
  /// \param Loc the source location of the directive.
  /// \param MacroNameTok Information on the token being tested.
  /// \param MD The MacroDefinition if the name was a macro, null otherwise.
  void Elifndef(SourceLocation DirectiveLocation, Token const& MacroNameTok, MacroDefinition const& MD) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Elifndef"));
  }

  /// Hook called whenever an \#elifndef is skipped.
  /// \param Loc the source location of the directive.
  /// \param ConditionRange The SourceRange of the expression being tested.
  /// \param IfLoc the source location of the \#if/\#ifdef/\#ifndef directive.
  void Elifndef(SourceLocation DirectiveLocation, SourceRange ConditionRange, SourceLocation IfLoc) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Elifndef"));
  }

  /// Hook called whenever an \#else is seen.
  /// \param Loc the source location of the directive.
  /// \param IfLoc the source location of the \#if/\#ifdef/\#ifndef directive.
  void Else(SourceLocation DirectiveLocation, SourceLocation IfLoc) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Else"));
  }

  /// Hook called whenever an \#endif is seen.
  /// \param Loc the source location of the directive.
  /// \param IfLoc the source location of the \#if/\#ifdef/\#ifndef directive.
  void Endif(SourceLocation DirectiveLocation, SourceLocation IfLoc) override
  {
    add_directive(DirectiveLocation COMMA_CWDEBUG_ONLY("Endif"));
  }
};
