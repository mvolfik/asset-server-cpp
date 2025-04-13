#include <memory>
#include <vector>
#include <iostream>
#include <fstream>

#include <vips/vips8>

// #include "src/image_processing.hpp"
// #include "src/server_state.hpp"
// #include "src/thread_pool.hpp"

using vips::VImage;

int
main(int argc, char* argv[])
{
  // read file to buffer:
  std::vector<std::uint8_t> buffer;
  std::ifstream file("/tmp/a.jpg", std::ios::binary);
  if (!file) {
    std::cerr << "Failed to open file" << std::endl;
    return 1;
  }
  file.seekg(0, std::ios::end);
  std::size_t size = file.tellg();
  file.seekg(0, std::ios::beg);
  buffer.resize(size);
  file.read(reinterpret_cast<char*>(buffer.data()), size);
  if (!file) {
    std::cerr << "Failed to read file" << std::endl;
    return 1;
  }
  file.close();

  if (VIPS_INIT("asset-server")) {
    throw std::runtime_error("Failed to initialize libvips");
  }

  auto img = std::make_shared<VImage>(VImage::new_from_buffer(buffer.data(), buffer.size(), nullptr));
  void* buf = nullptr;
  size = 0;

  img->write_to_buffer(".webp", &buf, &size);
  std::cout << "Buffer size: " << size << ", buffer: " << buf << std::endl;
  g_free(buf);

  vips_shutdown();
  return 0;
}
