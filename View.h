#pragma once

#include <string_view>
#include <memory>
#include <variant>
#ifdef DEBUG
#include <iosfwd>
#endif

class View
{
 private:
  using Data = std::variant<
    std::unique_ptr<char const*>,               // Allocated buffer with contiguous content.
    char const*                                 // Points to external data with a longer life-time that was already contiguous.
  >;

  enum Type {
    allocated,
    external
  };

  Type type_;
  Data data_;
  size_t size_;

 public:
  View(char const* contiguous_area, size_t size) : type_(external), data_(contiguous_area), size_(size) { }

  std::string_view realize() const;

#ifdef DEBUG
  friend std::ostream& operator<<(std::ostream& os, View const& view);
#endif
};
