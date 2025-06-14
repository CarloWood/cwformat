#include "sys.h"
#include "CodeScanner.h"

void test_decrement(CodeScanner const& code_scanner, int initial_offset, std::vector<char> expected)
{
  CodeScanner::iterator iter = code_scanner.get_iterator(initial_offset);
  int ei = 0;
  while (iter != code_scanner.one_before_begin())
  {
    ASSERT(ei >= 0);
    if (*iter != expected[ei])
      std::cout << "Failure: *iter returns '" << *iter << "', expected: '" << expected[ei] << "'.\n";
    ASSERT(*iter == expected[ei]);
    --iter;
    ++ei;
  }
}

void test_increment(CodeScanner const& code_scanner, int initial_offset, std::vector<char> expected)
{
  CodeScanner::iterator iter = code_scanner.get_iterator(initial_offset);
  int ei = 0;
  while (iter != code_scanner.end())
  {
    ASSERT(ei >= 0);
    if (*iter != expected[ei])
      std::cout << "Failure: *iter returns '" << *iter << "', expected: '" << expected[ei] << "'.\n";
    ASSERT(*iter == expected[ei]);
    ++iter;
    ++ei;
  }
}

int main()
{
  std::cout << "Test Case 0: Single line comment" << std::endl;
  CodeScanner scanner0("// foo\n");
  //                 -1 012345 67
  CodeScanner::iterator iter = scanner0.get_iterator(7);     // One past the end.
  --iter;
  ASSERT(iter == -1);   // One before the beginning.

  std::cout << "Test Case 1: Simple C++ comment" << std::endl;
  std::string_view src_for_test1 = "x = 1; // comment\n  y";
  // Indices:                    -1 01234567890123456 78901
  // Start at 'y' (idx 20)
  CodeScanner scanner1(src_for_test1);
  test_decrement(scanner1, 20, {'y', ';', '1', '=', 'x'});
  test_increment(scanner1, 0, {'x', '=', '1', ';', 'y'});

  std::cout << "Test Case 2: String literal containing /* and //" << std::endl;
  std::string_view sv_example = "MY_MACRO(\"This is the /* first // and only argument\"/*, optional_second_arg // or something. */)\nx";
  // Indices:                 -1 012345678 901234567890123456789012345678901234567890 123456789012345678901234567890123456789012345 678
  // Start at 'x' (idx 97)
  CodeScanner scanner2(sv_example);
  test_decrement(scanner2, 97, {'x', ')', '"', '"', '(', 'O', 'R', 'C', 'A', 'M', '_', 'Y', 'M'});
  test_increment(scanner2, 0, {'M', 'Y', '_', 'M', 'A', 'C', 'R', 'O', '(', '"', '"', ')', 'x'});

  std::cout << "Test Case 3: Mixed content" << std::endl;
  std::string_view sv_mixed = "std::string str = /*example:*/\"Hello world\"; // A string.\nint x =";
  // Indices:               -1 012345678901234567890123456789 012345678901 234567890123456 789012345
  // Start at '=' (idx 64)
  CodeScanner scanner_mixed(sv_mixed);
  test_decrement(scanner_mixed, 64, {'=', 'x', 't', 'n', 'i', ';', '"', '"', '=', 'r', 't', 's', 'g', 'n', 'i', 'r', 't', 's', ':', ':', 'd', 't', 's'});
  test_increment(scanner_mixed, 0, {'s', 't', 'd', ':', ':', 's', 't', 'r', 'i', 'n', 'g', 's', 't', 'r', '=', '"', '"', ';', 'i', 'n', 't', 'x', '='});

  std::cout << "Test Case 4: Whitespace and comments" << std::endl;
  std::string_view sv_whitespace = "a  /* com1 */ b // com2\n c";
  // Indices:                    -1 01234567890123456789012 3456
  // Start at 'c' (idx 25)
  CodeScanner scanner_whitespace(sv_whitespace);
  test_decrement(scanner_whitespace, 25, {'c', 'b', 'a'});
  test_increment(scanner_whitespace, 0, {'a', 'b', 'c'});

  std::cout << "Test Case 5: Char literal" << std::endl;
  std::string_view sv_char = "foo = '\\''; bar";
  // Indices:              -1 0123456 789012345
  // Start at 'r' (idx 14)
  CodeScanner scanner_char(sv_char);
  test_decrement(scanner_char, 14, {'r', 'a', 'b', ';', '\'', '\'', '=', 'o', 'o', 'f'});
  test_increment(scanner_char, 0, {'f', 'o', 'o', '=', '\'', '\'', ';', 'b', 'a', 'r'});

  std::cout << "Test Case 6: Empty string content" << std::endl;
  std::string_view sv_empty_str = "x = \"\"; y";
  // Indices:                   -1 0123 4 56789
  // Start at 'y' (idx 8)
  CodeScanner scanner_empty_str(sv_empty_str);
  test_decrement(scanner_empty_str, 8, {'y', ';', '"', '"', '=', 'x'});
  test_increment(scanner_empty_str, 0, {'x', '=', '"', '"', ';', 'y'});

  std::cout << "Test Case 7: Only C comment" << std::endl;
  std::string_view sv_only_comment = "/* only comment text */";
  // Indices:                      -1 012345678901234567890123
  // Start beyond comment (idx 23)
  CodeScanner scanner_only_comment(sv_only_comment);
  CodeScanner::iterator iter7 = scanner_only_comment.get_iterator(23);   // One past the end.
  --iter7;
  ASSERT(iter7 == -1);   // One before the beginning.

  std::cout << "Test Case 8: Unterminated block comment" << std::endl;
  std::string_view sv_unterm_block = "int x = /* foo";
  // Indices:                      -1 012345678901234
  // Start at '=' (idx 6)
  CodeScanner scanner_unterm_block(sv_unterm_block);
  test_decrement(scanner_unterm_block, 6, {'=', 'x', 't', 'n', 'i'});
  test_increment(scanner_unterm_block, 0, {'i', 'n', 't', 'x', '='});

  std::cout << "Test Case 9: A serious test" << std::endl;
  std::string_view sv_serious = R"(a/*b//c"d'e'f"g
h*/i///*j"k'l'm"*/
n"o/*p*/q"r'"'"'"s)";
  /* Indices:                      0123456789012345
6789012345678901234
567890123456789012 */
  // Start at 's' (idx 52)
  CodeScanner scanner_serious(sv_serious);
  test_decrement(scanner_serious, 52, {'s', '"', '"', '\'', '\'', 'r', '"', '"', 'n', 'i', 'a'});
  test_increment(scanner_serious, 0, {'a', 'i', 'n', '"', '"', 'r', '\'', '\'', '"', '"', 's'});
}
