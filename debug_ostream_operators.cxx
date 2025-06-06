#include "sys.h"
#ifdef CWDEBUG
#include "TranslationUnit.h"
#include "utils/macros.h"
#include "utils/print_using.h"
#include "utils/print_pointer.h"
#include "utils/print_range.h"
#include "debug_ostream_operators.h"
#include <vector>
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

char const* to_string(PPCallbacks::FileChangeReason file_change_reason)
{
  using enum PPCallbacks::FileChangeReason;
  switch (file_change_reason)
  {
    AI_CASE_RETURN(EnterFile);
    AI_CASE_RETURN(ExitFile);
    AI_CASE_RETURN(SystemHeaderPragma);
    AI_CASE_RETURN(RenameFile);
  }
  AI_NEVER_REACHED
}

char const* to_string(PPCallbacks::LexedFileChangeReason lexed_file_change_reason)
{
  using enum PPCallbacks::LexedFileChangeReason;
  switch (lexed_file_change_reason)
  {
    AI_CASE_RETURN(EnterFile);
    AI_CASE_RETURN(ExitFile);
  }
  AI_NEVER_REACHED
}

} // namespace clang

namespace debug {

void MacroInfo::print_on(std::ostream& os) const
{
  os << '{';
  os << "Location:" << print_source_location(macro_info_.getDefinitionLoc()) <<
      ", EndLocation:" << print_source_location(macro_info_.getDefinitionEndLoc()) <<
      ", ParameterList:" << utils::print_range(macro_info_.param_begin(), macro_info_.param_end(),
          [this](std::ostream& os, clang::IdentifierInfo* identifier_info){ os << IdentifierInfo{translation_unit_, *identifier_info}; }) <<
      ", ReplacementTokens:" << utils::print_range(macro_info_.tokens_begin(), macro_info_.tokens_end(),
          [this](std::ostream& os, clang::Token const& token){ os << print_token(token); }) <<
      ", NumParameters:" << macro_info_.getNumParams() <<
      ", NumReplacementTokens:" << macro_info_.getNumTokens() <<
      ", DefinitionLength:" << macro_info_.getDefinitionLength(translation_unit_.source_manager()) <<
      ", IsFunctionLike:" << std::boolalpha << macro_info_.isFunctionLike() <<
      ", IsC99Varargs:" << macro_info_.isC99Varargs() <<
      ", IsGNUVarargs:" << macro_info_.isGNUVarargs() <<
      ", IsBuiltinMacro:" << macro_info_.isBuiltinMacro() <<
      ", HasCommaPasting:" << macro_info_.hasCommaPasting() <<
      ", IsDisabled" << !macro_info_.isEnabled() <<
      ", IsUsed:" << macro_info_.isUsed() <<
      ", IsAllowRedefinitionsWithoutWarning:" << macro_info_.isAllowRedefinitionsWithoutWarning() <<
      ", IsWarnIfUnused:" << macro_info_.isWarnIfUnused() <<
      ", UsedForHeaderGuard:" << macro_info_.isUsedForHeaderGuard();
  os << '}';
}

void IdentifierInfo::print_on(std::ostream& os) const
{
  // Note: The order strictly follows the member variables in the clang::IdentifierInfo definition.
  // Name and Length conceptually come from 'Entry', which is near the end.
  os << '{';
  os << "name:" << identifier_info_.getName();
  // No need to print this.
  ASSERT(identifier_info_.getTokenID() == clang::tok::identifier);
//os << ", TokenID:" << identifier_info_.getTokenID();
  // This is extremely likely zero because we're not ObjC, nor are macro arguments builtin identifiers.
  if (identifier_info_.getObjCOrBuiltinID() != llvm::to_underlying(clang::InterestingIdentifier::NotInterestingIdentifier))
  {
    // InterestingIdentifierID broken down into its semantic parts via accessors
    if (identifier_info_.getObjCKeywordID() > 0)
      os << ", ObjCKeywordID:" << identifier_info_.getObjCKeywordID();
    if (identifier_info_.getBuiltinID() > 0)
      os << ", BuiltinID:" << identifier_info_.getBuiltinID();
    if (identifier_info_.getNotableIdentifierID() > 0)
      os << ", NotableIdentifierID:" << identifier_info_.getNotableIdentifierID();
  }
  std::vector<std::string> boolean_flags;
  // Boolean flags start here.
  if (identifier_info_.hasMacroDefinition())
    boolean_flags.emplace_back("HasMacro");
  if (identifier_info_.hadMacroDefinition())
    boolean_flags.emplace_back("HadMacro");
  if (identifier_info_.isExtensionToken())
    boolean_flags.emplace_back("IsExtension");
  if (identifier_info_.isFutureCompatKeyword())
    boolean_flags.emplace_back("IsFutureCompatKeyword");
  if (identifier_info_.isPoisoned())
    boolean_flags.emplace_back("IsPoisoned");
  if (identifier_info_.isCPlusPlusOperatorKeyword())
    boolean_flags.emplace_back("IsCPPOperatorKeyword");
  if (identifier_info_.isHandleIdentifierCase())
    boolean_flags.emplace_back("NeedsHandleIdentifier");
  if (identifier_info_.isFromAST())
    boolean_flags.emplace_back("IsFromAST");
  if (identifier_info_.hasChangedSinceDeserialization())
    boolean_flags.emplace_back("ChangedAfterLoad");
  if (identifier_info_.hasFETokenInfoChangedSinceDeserialization())
    boolean_flags.emplace_back("FEChangedAfterLoad");
  if (identifier_info_.hasRevertedTokenIDToIdentifier())
    boolean_flags.emplace_back("RevertedTokenID");
  if (identifier_info_.isOutOfDate())
    boolean_flags.emplace_back("OutOfDate");
  if (identifier_info_.isModulesImport())
    boolean_flags.emplace_back("IsModulesImport");
  if (identifier_info_.isMangledOpenMPVariantName())
    boolean_flags.emplace_back("IsMangledOpenMPVariantName");
  if (identifier_info_.isDeprecatedMacro())
    boolean_flags.emplace_back("IsDeprecatedMacro");
  if (identifier_info_.isRestrictExpansion())
    boolean_flags.emplace_back("IsRestrictExpansion");
  if (identifier_info_.isFinal())
    boolean_flags.emplace_back("IsFinal");
  if (!boolean_flags.empty())
  {
    os << ", {";
    char const* separator = "";
    for (std::string const& flag : boolean_flags)
    {
      os << separator << flag;
      separator = ", ";
    }
    os << "}";
  }
  // End boolean flags

  // Lets not print this if it is null, which it is basically always anyway.
  if (identifier_info_.getFETokenInfo())
    os << ", FETokenInfo:" << utils::print_pointer(identifier_info_.getFETokenInfo()); // Prints address of void*.
  os << '}';
}

} // namespace debug

#endif // CWDEBUG
