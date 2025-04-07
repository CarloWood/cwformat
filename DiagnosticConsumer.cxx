#include "sys.h"
#include "DiagnosticConsumer.h"
#include "utils/print_pointer.h"
#include "debug.h"

#ifdef CWDEBUG
#include "cwds/debug_ostream_operators.h"
#include "utils/macros.h"
#include "clang/Basic/LangOptions.h"

std::ostream& operator<<(std::ostream& os, clang::CommentOptions comment_options)
{
  os << '{';
  os <<   "BlockCommandNames: " << comment_options.BlockCommandNames;
  os << ", ParseAllComments: " << std::boolalpha << comment_options.ParseAllComments;
  os << '}';
  return os;
}

std::ostream& operator<<(std::ostream& os, clang::LangStandard::Kind kind)
{
  switch (kind)
  {
    #define LANGSTANDARD(id, name, lang, desc, features) \
      case clang::LangStandard::lang_##id: \
        return os << #id;
    #include "clang/Basic/LangStandards.def"
    default:
      return os << "lang_unspecified";
  }
  AI_NEVER_REACHED
}

std::ostream& operator<<(std::ostream& os, clang::LangOptions lang_options)
{
  os << '{';
  os <<   "LangStd: " << lang_options.LangStd;
  os << ", CommentOpts: " << lang_options.CommentOpts;
  os << '}';
  return os;
}

std::ostream& operator<<(std::ostream& os, clang::DiagnosticsEngine::Level level)
{
  switch (level)
  {
    case clang::DiagnosticsEngine::Ignored:
      return os << "Ignored";
    case clang::DiagnosticsEngine::Note:
      return os << "Note";
    case clang::DiagnosticsEngine::Warning:
      return os << "Warning";
    case clang::DiagnosticsEngine::Error:
      return os << "Error";
    case clang::DiagnosticsEngine::Fatal:
      return os << "Fatal";
    default:
      return os << "Unknown level";
  }
  AI_NEVER_REACHED
}

std::ostream& operator<<(std::ostream& os, clang::SourceLocation source_location)
{
  os << '{';
  os << "`source_location`";
  os << '}';
  return os;
}
#endif // CWDEBUG

void DiagnosticConsumer::BeginSourceFile(clang::LangOptions const& LangOpts, clang::Preprocessor const* PP)
{
  DoutEntering(dc::notice, "DiagnosticConsumer::BeginSourceFile: " << LangOpts << ", " << (void*)PP << ")");
}

void DiagnosticConsumer::EndSourceFile()
{
  DoutEntering(dc::notice, "DiagnosticConsumer::EndSourceFile()");
}

void DiagnosticConsumer::finish()
{
  DoutEntering(dc::notice, "DiagnosticConsumer::finish()");
}

void DiagnosticConsumer::HandleDiagnostic(clang::DiagnosticsEngine::Level level, clang::Diagnostic const &info)
{
  DoutEntering(dc::notice, "DiagnosticConsumer::HandleDiagnostic(" << level << ", " << info.getID() << ")");
  llvm::SmallVector<char> out_str(256);
  info.FormatDiagnostic(out_str);
  Dout(dc::notice, "DiagnosticConsumer::HandleDiagnostic: " << level << ", " << info.getID() << ", " <<
      info.getLocation() << ", " << info.getNumArgs() << ", " << info.getNumFixItHints() << ", " << info.getNumRanges() << ")");
}
