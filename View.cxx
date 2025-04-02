#include "View.h"
#include <cassert>

std::string_view View::realize() const
{
  // allocated is not implemented yet.
  assert(type_ == external);
  return {std::get<char const*>(data_), size_};
}

#ifdef DEBUG
std::ostream& operator<<(std::ostream& os, View const& view)
{
  return os << view.realize();
}
#endif
