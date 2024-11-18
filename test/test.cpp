#include <iostream>
#include <utility>

#include "test.hpp"

#include "../src/config.hpp"
#include "../src/utils.hpp"

void
test_parse_bytes()
{
  assert_eq(parse_bytes("123B"), 123u);
  assert_eq(parse_bytes("123k"), 123u * 1024);
  assert_eq(parse_bytes("123K"), 123u * 1024);
  assert_eq(parse_bytes("123M"), 123u * 1024 * 1024);
  assert_eq(parse_bytes("1G"), 1u * 1024 * 1024 * 1024);
}

void
test_sanitize_filename()
{
  assert_eq(sanitize_filename("abc"), "abc");
  assert_eq(sanitize_filename("abc def"), "abc_def");
  assert_eq(sanitize_filename("abc-def"), "abc-def");
  assert_eq(sanitize_filename("abc-def_"), "abc-def_");
  assert_eq(sanitize_filename("abc/def"), "abc_def");
  assert_eq(sanitize_filename("abc/../../../etc/hosts"),
            "abc__________etc_hosts");
}

void
test_remove_comment_and_trailing_whitespace()
{
  assert_eq(remove_comment_and_trailing_whitespace("abc # def # ghi jkl"),
            "abc");
  assert_eq(remove_comment_and_trailing_whitespace("abc    "), "abc");
  assert_eq(remove_comment_and_trailing_whitespace("abc   .#de"), "abc   .");
}

std::string
stringify_vec(std::vector<dimension_t> v)
{
  std::string res = "[";
  for (auto i : v) {
    if (res.size() > 1)
      res += ",";
    res += std::to_string(i);
  }
  res += "]";
  return res;
}

std::string
evaluated_size_spec(const char* s, dimension_t original_width)
{
  auto sizes = size_specs::parse(s).get_sizes(original_width);
  return stringify_vec(std::vector<dimension_t>(sizes.begin(), sizes.end()));
}

void
test_size_spec()
{
  assert_eq(evaluated_size_spec("100", 9815), "[100]");
  assert_eq(evaluated_size_spec("100", 85), "[100]");
  assert_eq(evaluated_size_spec("100,50:100px", 985),
            "[85,100,185,285,385,485,585,685,785,885,985]");
  assert_eq(evaluated_size_spec("256:10%", 1000),
            "[280,312,347,386,429,477,531,590,656,729,810,900,1000]");
}

#define T(name) { name, #name }

int
main()
{
  // array of pairs function+name:
  std::pair<void (*)(), const char*> tests[] = {
    T(test_parse_bytes),
    T(test_sanitize_filename),
    T(test_remove_comment_and_trailing_whitespace),
    T(test_size_spec),
  };

  int failed = 0;

  for (auto [test, name] : tests) {
    std::cout << "Running test: " << name << std::endl;
    try {
      test();
      std::cout << "  passed" << std::endl;
    } catch (std::exception const& e) {
      std::cerr << "  failed:\n  " << e.what() << std::endl;
      failed++;
    }
  }

  return failed > 0 ? 1 : 0;
}
