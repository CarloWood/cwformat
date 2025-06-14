#include "sys.h"
#include "CodeScanner.h"

CodeScanner::CodeScanner(std::string_view const& input) : input_(input), paren_level_(0)
{
  enum State {
    code,
    c_comment,
    cpp_comment,
    string_literal = '"',       // The closing char of a string literal.
    char_literal = '\''         // The closing char of a char literal.
  };
  State state = code;
  int region_start;     // Valid if state != code.
  int offset;           // Current offset.
  auto open_region = [&](State s){
    region_start = offset;
    state = s;
  };
  auto close_region = [&](){
    if (offset >= region_start)                                 // Do not add empty regions (can happen for empty string-literals).
      skippable_regions_.emplace_back(region_start, offset);    // offset is the region end (inclusive).
    state = code;
  };
  // Loop over all character pairs of the input.
  for (offset = 0; offset < input.size() - 1; ++offset)         // -1 so that `nc` can still be read.
  {
    char c = input[offset];
    char nc = input[offset + 1];
    if (c == '\\' && nc == '\n')
    {
      ++offset;                 // Skip all backslash-newlines.
      continue;
    }
    switch (state)
    {
      case code:
        if (c == '"' || c == '\'')
        {
          ++offset;             // Exclude the opening quote from the region.
          open_region(c == '"' ? string_literal : char_literal);
          --offset;             // In case it is the empty string-literal.
        }
        else if (c == '/' && nc == '*')
          open_region(c_comment);
        else if (c == '/' && nc == '/')
          open_region(cpp_comment);
        else if (c == '(')
        {
          if (paren_level_ == 0)
            parens_and_commas_.emplace_back(LParenCommaRParen::lparen, offset);
          ++paren_level_;
        }
        else if (c == ')')
        {
          --paren_level_;
          if (paren_level_ == 0)
            parens_and_commas_.emplace_back(LParenCommaRParen::rparen, offset);
        }
        else if (c == ',' && paren_level_ == 1)
          parens_and_commas_.emplace_back(LParenCommaRParen::comma, offset);
        break;
      case c_comment:
        if (c == '*' && nc == '/')
        {
          ++offset;             // Go to the '/'.
          close_region();
        }
        break;
      case cpp_comment:
        if (c == '\n')
          close_region();
        break;
      case string_literal:
      case char_literal:
        if (c == '\\')
          ++offset;             // Skip the next character.
        else if (c == static_cast<char>(state))
        {
          --offset;             // Exclude the closing quote from the region.
          close_region();
          ++offset;             // Skip over the closing quote.
        }
        break;
    }
  }
  // Handle the last character.
  offset = input.size() - 1;
  char c = input[offset];
  if (state != code)
    close_region();             // Pretend that the last character always closes an open region.
  else if (c == ')')            // We're not interested in opening a char-literal or string-literal, but we need to handle a closing brace.
  {
    --paren_level_;
    if (paren_level_ == 0)
      parens_and_commas_.emplace_back(LParenCommaRParen::rparen, offset);
  }
}

char CodeScannerIterator::operator*() const
{
  // Can not dereference end().
  ASSERT(code_scanner_.one_before_begin() < *this && *this < code_scanner_.end());
  return code_scanner_.get_character(offset_);
}

inline void CodeScannerIterator::init_skippable_range_left_and_right()
{
  // Initialize skippable_range_left_ from skippable_regions_index_left_.
  if (skippable_regions_index_left_ >= 0)
    skippable_range_left_ = code_scanner_.get_skippable_region(skippable_regions_index_left_).end;
  else
    skippable_range_left_ = -1;

  // Initialize skippable_range_right_ from skippable_regions_index_left_ + 1.
  int skippable_regions_index_right = skippable_regions_index_left_ + 1;
  if (skippable_regions_index_right < code_scanner_.number_of_skippable_regions())
    skippable_range_right_ = code_scanner_.get_skippable_region(skippable_regions_index_right).start;
  else
    skippable_range_right_ = code_scanner_.number_of_skippable_regions();
}

CodeScannerIterator::CodeScannerIterator(CodeScanner const& code_scanner, int offset) : code_scanner_(code_scanner), offset_(offset)
{
  skippable_regions_index_left_ = code_scanner.get_skippable_regions_index_left_of(offset);
  ASSERT(skippable_regions_index_left_ == -1 || code_scanner.get_skippable_region(skippable_regions_index_left_).end < offset);
  // offset is not allowed to point inside a skippable region.
  ASSERT(skippable_regions_index_left_ == code_scanner.number_of_skippable_regions() - 1 || code_scanner.get_skippable_region(skippable_regions_index_left_ + 1).start >= offset);
  init_skippable_range_left_and_right();
}

CodeScannerIterator& CodeScannerIterator::operator--()
{
  // Can't decrement when we reached the one-before-the-beginning position.
  ASSERT(offset_ != -1);
  while (--offset_ >= 0)
  {
    if (offset_ == skippable_range_left_)
    {
      offset_ = code_scanner_.get_skippable_region(skippable_regions_index_left_--).start;
      init_skippable_range_left_and_right();
      continue;
    }
    char c = code_scanner_.get_character(offset_);
    if (c == '\n' && offset_ > 0 && code_scanner_.get_character(offset_ - 1) == '\\')
    {
      // Skip the backslash-newline.
      --offset_;
      continue;
    }
    if (std::isspace(static_cast<unsigned int>(c)))
      continue;
    break;
  }
  return *this;
}

CodeScannerIterator& CodeScannerIterator::operator++()
{
  // Can't increment when we reached the end position.
  ASSERT(offset_ < code_scanner_.size());
  while (++offset_ < code_scanner_.size())
  {
    if (offset_ == skippable_range_right_)
    {
      offset_ = code_scanner_.get_skippable_region(++skippable_regions_index_left_).end;
      init_skippable_range_left_and_right();
      continue;
    }
    char c = code_scanner_.get_character(offset_);
    if (c == '\\' && offset_ < code_scanner_.size() - 1 && code_scanner_.get_character(offset_ + 1) == '\n')
    {
      // Skip the backslash-newline.
      ++offset_;
      continue;
    }
    if (std::isspace(static_cast<unsigned int>(c)))
      continue;
    break;
  }
  return *this;
}
