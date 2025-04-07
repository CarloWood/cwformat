#include "clang/Basic/Diagnostic.h"
#include "debug.h"

// The diagnostic consumer.
class DiagnosticConsumer : public clang::DiagnosticConsumer
{
 public:
  void BeginSourceFile(clang::LangOptions const& LangOpts, clang::Preprocessor const* PP) final;
  void EndSourceFile() final;
  void finish() final;

  void HandleDiagnostic(clang::DiagnosticsEngine::Level level, clang::Diagnostic const &info) final;
};

