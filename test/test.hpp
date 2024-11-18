#ifndef TEST_HPP
#define TEST_HPP

#include <string>
#include <string_view>

template<typename T>
std::string
universal_tostring(T s);

template<>
std::string
universal_tostring<std::string>(std::string s)
{
  return s;
}

template<>
std::string
universal_tostring<char const*>(char const* s)
{
  return s;
}

template<>
std::string
universal_tostring<char*>(char* s)
{
  return s;
}

template<>
std::string
universal_tostring<std::string_view>(std::string_view s)
{
  return std::string(s);
}

template<>
std::string
universal_tostring<int>(int s)
{
  return std::to_string(s);
}

template<>
std::string
universal_tostring<unsigned>(unsigned s)
{
  return std::to_string(s);
}

template<typename T, typename U>
void
assert_eq_(T a, U b, char const* a_str, char const* b_str)
{
  if (a != b) {
    throw std::runtime_error("Assertion failed: " + std::string(a_str) +
                             " != " + std::string(b_str) + "\n    left:  >>" +
                             universal_tostring(a) + "<<\n    right: >>" +
                             universal_tostring(b) + "<<");
  }
}

#define assert_eq(a, b) assert_eq_(a, b, #a, #b)

#endif // TEST_HPP