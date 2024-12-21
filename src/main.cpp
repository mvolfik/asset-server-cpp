#include <iostream>
#include <memory>
#include <utility>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "config.hpp"
#include "http_connection.hpp"
#include "image_processing.hpp"
#include "server_state.hpp"
#include "thread_pool.hpp"

void
http_server(boost::asio::ip::tcp::acceptor& acceptor,
            boost::asio::ip::tcp::socket& socket,
            server_state state)
{
  acceptor.async_accept(
    socket, [&acceptor, &socket, state](boost::beast::error_code ec) {
      // start the request, and "recurse" to accept next connection (it just
      // calls the function again, passing the references through, and the
      // previous call returns)
      if (!ec)
        std::make_shared<http_connection>(std::move(socket), state)->start();
      http_server(acceptor, socket, state);
    });
}

void
print_usage(char const* argv0)
{
  std::cerr << "Usage: " << argv0 << " [--config-file <file>]" << std::endl;
}

enum class argv_parse_state
{
  none,
  cfg_file,
};

int
main(int argc, char* argv[])
{
  const char* cfg_file = "asset-server.cfg";
  try {
    // load command-line arguments
    argv_parse_state state = argv_parse_state::none;
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
      } else if (state == argv_parse_state::cfg_file) {
        cfg_file = argv[i];
        state = argv_parse_state::none;
      } else if (strcmp(argv[i], "--config-file") == 0) {
        state = argv_parse_state::cfg_file;
      } else {
        throw std::runtime_error("Unknown argument: " + std::string(argv[i]));
      }
    }
    if (state == argv_parse_state::cfg_file)
      throw std::runtime_error("Expected argument for --config-file");
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    print_usage(argv[0]);
    return 1;
  }

  try {
    config cfg = config::parse(cfg_file);
    cfg.storage->init();

    thread_pool pool(cfg.get_thread_pool_size());

    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>>
      currently_processing;
    std::mutex currently_processing_mutex;

    server_state state{
      cfg, pool, currently_processing, currently_processing_mutex
    };
    init_image_processing(state);

    // prepare the boost async runtime
    boost::asio::io_context ctx;
    boost::asio::signal_set signals(ctx, SIGINT, SIGTERM);
    signals.async_wait(
      [&](boost::beast::error_code const&, int) { ctx.stop(); });

    boost::asio::ip::address address;
    try {
      address = boost::asio::ip::make_address_v4(cfg.listen_host);
    } catch (std::exception const& e) {
      throw std::runtime_error("Invalid listen_host: '" + cfg.listen_host +
                               "': " + e.what());
    }
    boost::asio::ip::tcp::acceptor acceptor{ ctx,
                                             { address, cfg.listen_port } };
    boost::asio::ip::tcp::socket socket{ ctx };

    std::cerr << "Listening on http://" << cfg.listen_host << ":"
              << cfg.listen_port << std::endl;
    http_server(acceptor, socket, state);

    ctx.run();
    pool.blocking_shutdown();

    destroy_image_processing(state);
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
