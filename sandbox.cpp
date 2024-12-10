#include <iostream>
#include <memory>

class Foo
{
private:
  int i;

public:
  Foo(int i)
    : i(i)
  {
    std::cerr << "Foo(" << i << ")\n";
  }
  void set(int i) { this->i = i; }
  int get() { return i; }
  ~Foo() { std::cerr << "~Foo(" << i << ")\n"; }
};

void
with_weak_inside()
{
  Foo* raw_ptr;

  {
    std::weak_ptr<Foo> weak;

    // this creates a single allocation that contains both the shared_ptr
    // control data and Foo itself
    auto shared = std::make_shared<Foo>(42);
    weak = shared;
    raw_ptr = shared.get();
    // here both shared and weak are destroyed, so the whole allocation is freed
  }

  // memory error caught by -fsanitize=address
  std::cerr << "value: " << raw_ptr->get() << std::endl;
}

void
with_new()
{
  Foo* raw_ptr;

  std::weak_ptr<Foo> weak;
  {
    // this is a different way to create a shared_ptr - Foo is allocated
    // separately from the shared_ptr control data
    std::shared_ptr<Foo> shared(new Foo(42));
    weak = shared;
    raw_ptr = shared.get();
    // now last strong reference is destroyed, so the allocation created by `new
    // Foo(42)` is freed, but the control structure remains, since it still
    // needs the weak pointer counter
  }

  // memory error caught by -fsanitize=address
  std::cerr << "value: " << raw_ptr->get() << std::endl;
}

void
with_weak_outside()
{
  Foo* raw_ptr;

  std::weak_ptr<Foo> weak;
  {
    // this creates a single allocation that contains both the shared_ptr
    // control data and Foo itself
    auto shared = std::make_shared<Foo>(42);
    weak = shared;
    raw_ptr = shared.get();
    // last strong reference is destroyed here, but we still need the weak
    // pointer counter. So the Foo object is destroyed, but the underlying
    // memory is not deallocated!
  }

  // BAM: this is just as invalid code, but the address sanitizer doesn't catch
  // it!
  std::cerr << "value: " << raw_ptr->get() << std::endl;
}

int
main()
{
  with_new();
  with_weak_inside();
  with_weak_outside();
}
