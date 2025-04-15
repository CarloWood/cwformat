#include "sys.h"
#ifdef CWDEBUG
#include "utils/macros.h"
#include "debug_ostream_operators.h"
#endif
#include "debug.h"

#ifdef CWDEBUG
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

char const* to_string(MacroDirective::Kind macro_directive_kind)
{
  using enum MacroDirective::Kind;
  switch (macro_directive_kind)
  {
    AI_CASE_RETURN(MD_Define);
    AI_CASE_RETURN(MD_Undefine);
    AI_CASE_RETURN(MD_Visibility);
  }
  AI_NEVER_REACHED
}

} // namespace clang

#endif
