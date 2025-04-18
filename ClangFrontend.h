#pragma once

#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Lexer.h"
#include "llvm/TargetParser/Host.h"
#include <iostream>
#include <memory>
#include <string>
#include "DiagnosticConsumer.h"
#include "SourceFile.h"

// Forward declaration.
class TranslationUnit;

struct DiagnosticOptions : clang::DiagnosticOptions
{
  DiagnosticOptions()
  {
    IgnoreWarnings = true;
    //NoRewriteMacros = false;
    //Pedantic = false;
    //PedanticErrors = false;
    //ShowLine = true;
    //ShowColumn = true;
    //ShowLocation = true;
    //ShowLevel = true;
    //AbsolutePath = false;
    //ShowCarets = true;
    //ShowFixits = true;
    //ShowSourceRanges = false;
    //ShowParseableFixits = false;
    //ShowPresumedLoc = false;
    //ShowOptionNames = false;

    //ShowNoteIncludeStack = false;

    // 0 (Default): "None"
    // Do not show the diagnostic category name or ID.
    // Corresponds to -fdiagnostics-show-category=none.
    // 1: "ID"
    // Show the category number (an internal integer ID). This is rarely useful unless you are debugging the diagnostics system itself.
    // Corresponds to -fdiagnostics-show-category=id.
    // 2: "Name"
    // Show the category name (e.g., "Semantic Issue", "Lexical Issue", "Parse Issue"). This can sometimes be helpful for understanding the general area a diagnostic comes from.
    // Corresponds to -fdiagnostics-show-category=name.
    //ShowCategories = 0;

    //setFormat(Clang);

    ShowColors = true;
    //UseANSIEscapeCodes = false;
    //setShowOverloads(clang::Ovl_All);

    //VerifyDiagnostics = false;

    //setVerifyIgnoreUnexpected(clang::DiagnosticLevelMask::None);

    //ElideType = false;
    //ShowTemplateTree = false;

    //ErrorLimit = 0;
    //MacroBacktraceLimit = clang::DiagnosticOptions::DefaultMacroBacktraceLimit;
    //TemplateBacktraceLimit = clang::DiagnosticOptions::DefaultTemplateBacktraceLimit;
    //ConstexprBacktraceLimit = clang::DiagnosticOptions::DefaultConstexprBacktraceLimit;
    //SpellCheckingLimit = clang::DiagnosticOptions::DefaultSpellCheckingLimit;
    //SnippetLineLimit = clang::DiagnosticOptions::DefaultSnippetLineLimit;
    //ShowLineNumbers = clang::DiagnosticOptions::DefaultShowLineNumbers;
    //TabStop = clang::DiagnosticOptions::DefaultTabStop;
    //MessageLength = 0;

    //ShowSafeBufferUsageSuggestions = false;
  }
};

struct FileSystemOptions : public clang::FileSystemOptions
{
  FileSystemOptions()
  {
    // If set, paths are resolved as if the working directory was set to the value of WorkingDir.
    //WorkingDir.clear();
  }
};

struct LangOptions : public clang::LangOptions
{
  LangOptions()
  {
    // Initialize the language options.
    CPlusPlus = true;
    CPlusPlus20 = true;
    Bool = 1;
    CXXOperatorNames = 1;
    //CPlusPlus11 = true;
    //CPlusPlus14 = true;
    //CPlusPlus17 = true;
    //CPlusPlus2a = true;
  }
};

struct HeaderSearchOptions : public clang::HeaderSearchOptions
{
  HeaderSearchOptions()
  {
    UseBuiltinIncludes = false;
    UseStandardSystemIncludes = false;
    // Configure HeaderSearchOptions (if needed).
    //AddPath("/usr/include", clang::frontend::Angled, false, false);
  }
};

struct PreprocessorOptions : public clang::PreprocessorOptions
{
  PreprocessorOptions()
  {
    UsePredefines = false; // Do not load builtin definitions/macros.
    // Example options:
    //AddMacroDef("NDEBUG");
  }
};

struct TargetOptions : public clang::TargetOptions
{
  TargetOptions()
  {
    Triple = llvm::sys::getDefaultTargetTriple();
    // Example options:
    //Features.push_back("+sse");
  }
};

class OptionsBase
{
 protected:
  llvm::IntrusiveRefCntPtr<DiagnosticOptions> diagnostic_options_{new DiagnosticOptions};
  FileSystemOptions file_system_options_;
  LangOptions lang_options_;
  HeaderSearchOptions header_search_options_; // llvm-project master. For older versions use:
  //std::shared_ptr<clang::HeaderSearchOptions> header_search_options_{std::make_shared<HeaderSearchOptions>()};
  std::shared_ptr<clang::PreprocessorOptions> preprocessor_options_{std::make_shared<PreprocessorOptions>()};
  std::shared_ptr<clang::TargetOptions> target_options_{std::make_shared<TargetOptions>()};
};

class ClangFrontend : public OptionsBase
{
 private:
  // Diagnostics Infrastructure.
  //  DiagnosticConsumer diagnostic_consumer_;
  clang::TextDiagnosticPrinter diagnostic_consumer_;
  llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagnostic_ids_;
  mutable clang::DiagnosticsEngine diagnostics_engine_;

  // Language and Target.
  llvm::IntrusiveRefCntPtr<clang::TargetInfo> target_info_;

  // Source File Management.
  clang::FileManager file_manager_;
  clang::SourceManager source_manager_;

  // Preprocessor Infrastructure.
  clang::HeaderSearch header_search_;
  clang::TrivialModuleLoader module_loader_;

 public:
  ClangFrontend();

  void begin_source_file(SourceFile const& source_file, TranslationUnit& translation_unit);
  void end_source_file();

  clang::SourceManager const& source_manager() const { return source_manager_; }

  std::pair<unsigned int, size_t> measure_token_length(clang::SourceLocation location)
  {
    return {source_manager_.getFileOffset(location), clang::Lexer::MeasureTokenLength(location, source_manager_, lang_options_)};
  }

  // Reads from input_buffer and writes to translation_unit.
  void process_input_buffer(SourceFile const& source_file, TranslationUnit& translation_unit) const;

 private:
  static clang::TargetInfo* create_target_info(
    clang::DiagnosticsEngine& diagnostics_engine, std::shared_ptr<clang::TargetOptions> const& target_options);
};
