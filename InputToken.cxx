#include "sys.h"
#include "InputToken.h"
#include "debug.h"

#ifdef CWDEBUG
void PPToken::print_on(std::ostream& os) const
{
  os << "<" << getTokenName(kind_) << ">";
//  if (kind_ == directive || kind_ == header_name || kind_ == pragma)
//    os << " [" << content_ << "]";
}
#endif
