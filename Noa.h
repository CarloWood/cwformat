#pragma once

#include <utility>
#include <memory>
#include <type_traits>
#include <concepts>

class Noa;

template<typename T>
concept ConceptNoa = std::derived_from<T, Noa>;

enum NoaTypes {
  leaf,
  container
};

class Noa
{
 public:
  NoaTypes type_;

 protected:
  virtual void print_real(std::ostream& os) const = 0;

 public:
  Noa(NoaTypes type) : type_(type) { }
  virtual ~Noa() = default;
  Noa(Noa const&) = delete;     // Disallow copying or moving; we'll be using pointers to Noa's.

  // Accessor.
  NoaTypes type() const { return type_; }

  // Create a new Noa.
  template<ConceptNoa T, typename... Args>
  static std::unique_ptr<T> create(Args&&... args);

  void print(std::ostream& os) const
  {
    print_real(os);
  }
};

//static
template<ConceptNoa T, typename... Args>
std::unique_ptr<T> Noa::create(Args&&... args)
{
  return make_unique<T>(std::forward<Args>(args)...);
}
