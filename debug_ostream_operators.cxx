#include "sys.h"
#include "TranslationUnit.h"
#include "SourceFile.h"
#include "utils/macros.h"
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Token.h>
#ifdef CWDEBUG
#include "debug_ostream_operators.h"
#endif
#include "debug.h"

namespace clang {
namespace SrcMgr {

char const* to_string(clang::SrcMgr::CharacteristicKind characteristic_kind)
{
  switch (characteristic_kind)
  {
    AI_CASE_RETURN(C_User);
    AI_CASE_RETURN(C_System);
    AI_CASE_RETURN(C_ExternCSystem);
    AI_CASE_RETURN(C_User_ModuleMap);
    AI_CASE_RETURN(C_System_ModuleMap);
  }
  AI_NEVER_REACHED
}

} // namespace SrcMgr
} // namespace clang

namespace debug {

void SourceLocation::print_on(std::ostream& os) const
{
  if (location_.isInvalid())
  {
    os << "<invalid SourceLocation>";
    return;
  }

  clang::SourceManager const& source_manager = translation_unit_.source_manager();
  if (location_.isMacroID())
    os << "<macro>" << location_.printToString(source_manager) << "</macro>";
  else
  {
    std::pair<clang::FileID, unsigned int> location = source_manager.getDecomposedLoc(location_);
    unsigned int line = source_manager.getLineNumber(location.first, location.second);
    unsigned int column = source_manager.getColumnNumber(location.first, location.second);
    os << line << ":" << column;
  }
}

void SourceRange::print_on(std::ostream& os) const
{
  SourceLocation begin{translation_unit_, source_range_.getBegin()};
  SourceLocation end{translation_unit_, source_range_.getEnd()};
  begin.print_on(os);
  os << " - ";
  end.print_on(os);
}

void CharSourceRange::print_on(std::ostream& os) const
{
  if (char_source_range_.isInvalid())
  {
    os << "<invalid CharSourceRange>";
    return;
  }

  SourceLocation begin{translation_unit_, char_source_range_.getBegin()};
  SourceLocation end{translation_unit_, char_source_range_.getEnd()};

  clang::SourceManager const& source_manager = translation_unit_.source_manager();
  std::pair<clang::FileID, unsigned int> begin_location = source_manager.getDecomposedLoc(begin.location_);
  std::pair<clang::FileID, unsigned int> end_location = source_manager.getDecomposedLoc(end.location_);

  ASSERT(begin_location.first == end_location.first);

  char const* prefix = "";
  char const* separator = " ~ ";
  char const* postfix = "";
  if (char_source_range_.isCharRange())
  {
    ASSERT(begin_location.second <= end_location.second);
    size_t size = end_location.second - begin_location.second;
    os << '"' << libcwd::buf2str(translation_unit_.source_file().span(begin_location.second, size)) << '"';
    prefix = " [";
    postfix = ">";
    separator = ", ";
  }

  os << prefix;
  begin.print_on(os);
  os << separator;
  end.print_on(os);
  os << postfix;
}

void Token::print_on(std::ostream& os) const
{
  using namespace clang;
  tok::TokenKind kind = token_.getKind();
  os << "<" << getTokenName(kind) << ">" << " @ " << PrintSourceLocation{translation_unit_}(token_.getLocation());
}

} // namespace debug
