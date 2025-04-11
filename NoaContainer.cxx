#include "sys.h"
#include "NoaContainer.h"
#include <iostream>

void NoaContainer::print_real(std::ostream& os) const
{
  os << "NoaContainer: ";
  for (auto const& child : children_)
    child->print(os);
}
