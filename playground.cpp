#include <memory>
#include <vector>

#include <vips/vips8>

#include "src/image_processing.hpp"
#include "src/server_state.hpp"
#include "src/thread_pool.hpp"

using vips::VImage;

void
task(int groupi, int i, std::shared_ptr<task_group> ptr)
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

  config cfg = config::parse("../asset-server.cfg");

  std::atomic<bool> done = false;
  std::atomic<bool> err = false;

  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
    currently_processing;
  std::mutex currently_processing_mutex;

  auto handler = [&done, &err](std::exception const* e) {
    if (e) {
      std::cerr << "Error: " << e->what() << std::endl;
      err = true;
    }

    done = true;
  };

  std::ifstream file("a.png", std::ios::binary | std::ios::ate);
  auto pos = file.tellg();
  if (pos < 0)
    throw std::runtime_error("Failed to open file");
  std::vector<std::uint8_t> file_content(pos);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(file_content.data()), file_content.size());

  image_processor processor(
    { cfg, pool, currently_processing, currently_processing_mutex },
    handler,
    file_content,
    "čáíěšěíášřýěšřěýěíšýěřšěíýšěřýšříýřšě.png");

  while (!done)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  if (err) {
    pool.blocking_shutdown();
    return 1;
  }

  auto& response_stream = std::cout;
  response_stream << "{\"filename\": \"" << processor.get_filename()
                  << "\", \"hash\": \"" << processor.get_hash()
                  << "\", \"original\": \"" << processor.get_original_format()
                  << "\", \"variants\": [";

  bool first = true;
  for (auto const& d : processor.get_dimensions()) {
    if (!first)
      response_stream << ", ";
    first = false;
    d.write_json(response_stream);
  }
  response_stream << "]}\n";

  pool.blocking_shutdown();

  // VImage img = VImage::new_from_file("a.png", NULL);

  // img = img.thumbnail_image(128);
  // img.write_to_file("b.jpeg");

  return 0;
}
