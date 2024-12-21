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
process_image(std::string const& filename, server_state state)
{
  std::atomic<bool> done = false;
  std::atomic<bool> err = false;

  auto handler = [&done, &err](std::exception const* e) {
    if (e) {
      std::cerr << "Error: " << e->what() << std::endl;
      err = true;
    }

    done = true;
  };

  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  auto pos = file.tellg();
  if (pos < 0)
    throw std::runtime_error("Failed to open file");
  std::vector<std::uint8_t> file_content(pos);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(file_content.data()), file_content.size());

  auto processor =
    image_processor::create(state, handler, file_content, filename);

  while (!done)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::cerr << "Done!\n";
  if (err) {
    return 1;
  }

  auto& response_stream = std::cout;
  response_stream << "{\"filename\": \"" << processor->get_filename()
                  << "\", \"hash\": \"" << processor->get_hash()
                  << "\", \"original\": ";
  processor->get_original().write_json(response_stream);
  response_stream << ", \"variants\": [";

  bool first = true;
  for (auto const& d : processor->get_dimensions()) {
    if (!first)
      response_stream << ", ";
    first = false;
    d.write_json(response_stream);
  }
  response_stream << "], \"is_new\": "
                  << (processor->get_is_new() ? "true" : "false") << "}\n";
  return 0;
}

int
main(int argc, char* argv[])
{
  thread_pool pool(12);

  config cfg = config::parse("../asset-server.cfg");
  cfg.storage->init();

  std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
    currently_processing;
  std::mutex currently_processing_mutex;

  server_state state{
    cfg, pool, currently_processing, currently_processing_mutex
  };
  init_image_processing(state);

  std::string filename("/tmp/a.png");
  if (argc > 1)
    filename = argv[1];

  int ret = process_image(filename, state);
  if (ret != 0) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  pool.blocking_shutdown();

  destroy_image_processing(state);

  return ret;
}
