#include <iostream>
#include <memory>

void
with_new()
{
  int* raw_ptr;

  std::weak_ptr<int> weak;
  {
    std::shared_ptr<int> shared(new int(42));
    weak = shared;
    raw_ptr = shared.get();
  }

  // memory error
  std::cerr << "value: " << *raw_ptr << std::endl;
}

void
with_weak_inside()
{
  int* raw_ptr;

  {
    std::weak_ptr<int> weak;
    auto shared = std::make_shared<int>(42);
    weak = shared;
    raw_ptr = shared.get();
  }

  // memory error
  std::cerr << "value: " << *raw_ptr << std::endl;
}

void
with_weak_outside()
{
  int* raw_ptr;

  std::weak_ptr<int> weak;
  {
    auto shared = std::make_shared<int>(42);
    weak = shared;
    raw_ptr = shared.get();
  }

  // this is just as invalid code, but the address sanitizer doesn't catch it!
  std::cerr << "value: " << *raw_ptr << std::endl;
}

int
main()
{
  with_new();
  with_weak_inside();
  with_weak_outside();
}
