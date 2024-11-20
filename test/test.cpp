#include <iostream>
#include <utility>

#include "test.hpp"

#include "../src/config.hpp"
#include "../src/storage/fs.hpp"
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
  assert_eq(sanitize_filename("abcčťäåαß"), "abcctaaass");
}

void
test_remove_comment_and_trailing_whitespace()
{
  assert_eq(remove_comment_and_trailing_whitespace("abc # def # ghi jkl"),
            "abc");
  assert_eq(remove_comment_and_trailing_whitespace("abc    "), "abc");
  assert_eq(remove_comment_and_trailing_whitespace("abc   .#de"), "abc   .");
}

void
test_get_filename_without_extension()
{
  assert_eq(get_filename_without_extension("abc"), "abc");
  assert_eq(get_filename_without_extension("abc.def"), "abc");
  assert_eq(get_filename_without_extension("abc.def.ghi"), "abc.def");
  assert_eq(get_filename_without_extension("abc.def.ghi.jkl"), "abc.def.ghi");
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

void
test_sha256()
{
  // create tests with:
  // python3 -c "import sys; sys.stdout.buffer.write(bytes([ ... ]))" |
  // sha256sum
  assert_eq(sha256({}),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  assert_eq(
    sha256({ 69,  99,  64,  12,  183, 133, 140, 35,  52,  235, 137, 199, 247,
             125, 0,   171, 158, 246, 253, 46,  192, 15,  66,  233, 243, 159,
             4,   117, 132, 28,  138, 43,  117, 30,  230, 47,  122, 87,  127,
             43,  113, 180, 41,  105, 125, 56,  25,  194, 182, 217, 194, 127,
             7,   67,  161, 90,  246, 121, 144, 230, 111, 161, 54,  150, 249,
             237, 233, 6,   166, 184, 0,   220, 229, 20,  152, 131, 115, 191,
             149, 233, 38,  212, 163, 156, 104, 33,  18,  45,  50,  103, 30,
             50,  72,  62,  224, 163, 191, 242, 94,  3 }),
    "a1c9081c7605668edfc136831c1f59a657a4e27809a7a13d508c857539273a91");
}

void
test_fs_walk_folder()
{
  storage_fs fs;
  fs.set_config("data_dir", "..");
  fs.set_config("temp_dir", "/tmp/asset-server-test");
  fs.validate();
  fs.init();

  // simple test: let's find src/storage/fs.hpp
  bool found = false;

  auto src = fs.walk_folder("src");
  if (src == std::nullopt)
    throw std::runtime_error("walk_folder returned nullopt on src folder");

  for (auto const& entry : *src) {
    if (entry.name == "storage") {
      if (entry.children == std::nullopt)
        throw std::runtime_error("src/storage folder should have children");

      for (auto const& child : *entry.children) {
        if (child.name == "fs.hpp") {
          found = true;
          break;
        }
      }
    }
  }

  if (!found)
    throw std::runtime_error("src/storage/fs.hpp not found");
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
    T(test_get_filename_without_extension),
    T(test_sha256),
    T(test_fs_walk_folder),
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
