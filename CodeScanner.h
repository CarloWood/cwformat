#pragma once

#include <vector>
#include <string_view>
#include "debug.h"

struct SkippableRegion
{
  int start;    // Index of the first character covered by this skipping rule (e.g., first char of comment,
                // or the first char after an opening quote of a string-literal).
  int end;      // Index of the character the last character covered by this rule
                // (e.g., the newline of a C++-comment or the `/` of a C-comment closing, or the character one before the closing quote of a string-literal).
};

struct LParenCommaRParen
{
  enum Kind { lparen, comma, rparen } kind_;
  unsigned int offset_; // The position of the parenthesis or comma.
};

class CodeScanner;

class CodeScannerIterator
{
 private:
  CodeScanner const& code_scanner_;
  int offset_;                          // Current offset into code_scanner_.input_.
  int skippable_regions_index_left_;    // The index into code_scanner_.skippable_regions_ for the nearest SkippableRegion on the left.
  int skippable_range_left_;            // Cached value of code_scanner_.skippable_regions_[skippable_regions_index_left_].end.
  int skippable_range_right_;           // Cached value of code_scanner_.skippable_regions_[skippable_regions_index_left_ + 1].start.

 public:
  // Construct an iterator that points one past the end, or one before the beginning (can be used for both sides).
  CodeScannerIterator(CodeScanner const& code_scanner) : code_scanner_(code_scanner), offset_(-1) { }   // end()
  // Construct an iterator that points at the character at offset `offset`.
  CodeScannerIterator(CodeScanner const& code_scanner, int offset);

  // Advance backwards to the previous character, skipping over
  // white-space, C- and C++-comments and the contents of string-
  // and char-literals.
  CodeScannerIterator& operator--();

  // Advance forwards to the next character, skipping over
  // white-space, C- and C++-comments and the contents of string-
  // and char-literals.
  CodeScannerIterator& operator++();

  // Dereference the iterator, returning the character it points to.
  char operator*() const;
  // Return the offset relative to the start of code_scanner_.input_.
  int offset() const { return offset_; }

  bool operator==(int offset) const
  {
    return offset == offset_;
  }

  bool operator==(CodeScannerIterator const& iter) const
  {
    // Must refer to the same string_view.
    ASSERT(&code_scanner_ == &iter.code_scanner_);
    return offset_ == iter.offset_;
  }

  bool operator!=(CodeScannerIterator const& iter) const
  {
    // Must refer to the same string_view.
    ASSERT(&code_scanner_ == &iter.code_scanner_);
    return offset_ != iter.offset_;
  }

  bool operator<(CodeScannerIterator const& iter) const
  {
    // Must refer to the same string_view.
    ASSERT(&code_scanner_ == &iter.code_scanner_);
    return offset_ < iter.offset_;
  }

  friend int operator-(CodeScannerIterator const& lhs, CodeScannerIterator const& rhs)
  {
    return lhs.offset_ - rhs.offset_;
  }

 private:
  void init_skippable_range_left_and_right();
};

class CodeScanner
{
 public:
  using iterator = CodeScannerIterator;

 private:
  std::string_view input_;                              // Code snippet that begins and ends outside string- and char-literals, as well as outside comments.
  std::vector<SkippableRegion> skippable_regions_;
  std::vector<LParenCommaRParen> parens_and_commas_;    // The positions of the opening '(', all level-one comma's and the closing ')'.
  int paren_level_;                                     // The number of open parenthesis.

 public:
  CodeScanner(std::string_view const& input);

  iterator get_iterator(int offset) const
  {
    return {*this, offset};
  }

  int size() const
  {
    return static_cast<int>(input_.length());
  }

  iterator one_before_begin() const
  {
    return {*this};
  }

  iterator begin() const
  {
    return {*this, 0};
  }

  iterator end() const
  {
    return {*this, size()};
  }

  int number_of_skippable_regions() const
  {
    return skippable_regions_.size();
  }

  SkippableRegion const& get_skippable_region(int region_index) const
  {
    return skippable_regions_[region_index];
  }

  // `offset` is not allowed to be pointing inside a skippable region.
  // For example:
  //                      start          end    Note: [start, end] are inclusive.
  //                        v             v
  //   <---region1--->[code]<---region2--->
  //   ^              ^     ^
  //   |              |     |
  //   C              A     B
  // All offsets in the interval [A, B] have region1 as left region and region2 as right region.
  // Therefore the index of the right-region is one larger than the index of the left-region.
  // Note that C has region1 as right-region and region1 - 1 as left-region.
  //
  // `offset` is not allowed to be smaller than A or larger than B (unless it completely skips the respective region).
  //
  // If a region doesn't exist, -1 is returned for the left-region and number_of_regions is returned
  // for the right-region. For example, if there are only three regions, with indices 0, 1 and 2:
  //
  //   [code]<--0-->[code]<--1-->[code]<--2-->[code]    Note: all `[code]` intervals are optional.
  //   ^^^^^^^                                ^^^^^^
  //      |                                      |
  //      A                                      B
  //
  // then the left-region of A doesn't exist and -1 is returned, while the right-region
  // of B (which can only exist if the trailing [code] exists) also doesn't exist and 3 is returned.
  //
  int get_skippable_regions_index_left_of(int offset) const
  {
    int region_index = skippable_regions_.size() - 1;   // The largest possible index. This corresponds with not having an existing right-region.
    while(region_index >= 0)
    {
      if (skippable_regions_[region_index].end < offset)
        break;
      --region_index;
    }
    return region_index;
  }

  char get_character(int offset) const
  {
    return input_[offset];
  }

  std::vector<LParenCommaRParen> const& parens_and_commas() const
  {
    return parens_and_commas_;
  }
};
