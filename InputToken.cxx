#include "sys.h"
#include "InputToken.h"
#include "utils/macros.h"
#include "debug.h"

char const* to_string(PPToken::Kind kind)
{
  using enum PPToken::Kind;
  switch (kind)
  {
     AI_CASE_RETURN(whitespace);
     AI_CASE_RETURN(c_comment);
     AI_CASE_RETURN(cxx_comment);
     AI_CASE_RETURN(escaped_newline);
     AI_CASE_RETURN(directive_hash);
     AI_CASE_RETURN(directive);
     AI_CASE_RETURN(macro_name);
     AI_CASE_RETURN(function_macro_name);
     AI_CASE_RETURN(function_macro_lparen);
     AI_CASE_RETURN(function_macro_rparen);
     AI_CASE_RETURN(function_macro_param);
     AI_CASE_RETURN(function_macro_comma);
     AI_CASE_RETURN(function_macro_ellipsis);
//     AI_CASE_RETURN(macro_definition);
//     AI_CASE_RETURN(macro_usage);
     AI_CASE_RETURN(header_name);
     AI_CASE_RETURN(pragma);
  }
  AI_NEVER_REACHED
}

#ifdef CWDEBUG
void PPToken::print_on(std::ostream& os) const
{
  os << "<" << getTokenName(kind_) << ">";
//  if (kind_ == directive || kind_ == header_name || kind_ == pragma)
//    os << " [" << content_ << "]";
}
#endif
