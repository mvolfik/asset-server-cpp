#include <memory>
#include <vector>

#include <vips/vips8>

#include "src/thread_pool.hpp"

using vips::VImage;

template<typename OnFinish>
void
task(int groupi, int i, std::shared_ptr<task_group<OnFinish>> ptr)
{
  int N = 10;
  std::cerr << "Task " << i << "/" << groupi << " started\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  if (i % N != 0) {
    ptr->add_task([groupi, i, ptr]() { task(groupi, i + 1, ptr); });
  }
  if (i == N - 2) {
    throw std::runtime_error("Task failed aaa");
  }
  std::cerr << "Task " << i << "/" << groupi << " finished\n";
}

int
main(int argc, char* argv[])
{
  if (VIPS_INIT(argv[0]))
    vips_error_exit(NULL);

  thread_pool pool(1);

  auto onFinishFactory = [](int groupi) {
    return [groupi]() { std::cerr << "Group " << groupi << " finished\n"; };
  };

  using groupt = task_group<decltype(onFinishFactory(0))>;

  std::vector<std::shared_ptr<groupt>> groups;

  for (int i = 1; i <= 3; i++) {
    auto ptr = std::make_shared<groupt>(
      pool,
      [i](std::exception const& e) {
        std::cerr << "Error in group " << i << ": " << e.what() << std::endl;
      },
      onFinishFactory(i));
    groups.push_back(ptr);

    ptr->add_task([i, ptr]() {
      std::cerr << "Group " << i << " started\n";
      task(i, 1, ptr);
    });
    ptr->add_task([i, ptr]() {
      std::cerr << "Group " << i << " started\n";
      task(i, 21, ptr);
    });
  }

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // VImage img = VImage::new_from_file("a.png", NULL);

  // img = img.thumbnail_image(128);
  // img.write_to_file("b.jpeg");

  return 0;
}
