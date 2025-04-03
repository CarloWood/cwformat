#include "sys.h"
#include "View.h"
#include "debug.h"

std::string_view View::realize() const
{
  // allocated is not implemented yet.
  ASSERT(type_ == external);
  return {std::get<char const*>(data_), size_};
}

#ifdef CWDEBUG
void View::print_on(std::ostream& os) const
{
  os << realize();
}
#endif
