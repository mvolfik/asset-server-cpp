#include "main.hpp"

void
http_server(boost::asio::ip::tcp::acceptor& acceptor,
            boost::asio::ip::tcp::socket& socket,
            http_connection::worker_pool_t& pool)
{
  acceptor.async_accept(socket, [&](boost::beast::error_code ec) {
    // start the request, and "recurse" to accept next connection (it just
    // calls the function again, passing the references through, and the
    // previous call returns)
    if (!ec)
      std::make_shared<http_connection>(std::move(socket), pool)->start();
    http_server(acceptor, socket, pool);
  });
}

int
main(int argc, char* argv[])
{
  try {
    const char* ip = "0.0.0.0";
    unsigned short port = 8000;
    std::cerr << "Listening on http://" << ip << ":" << port << std::endl;

    boost::asio::io_context ctx;

    auto address = boost::asio::ip::make_address_v4(ip);
    boost::asio::ip::tcp::acceptor acceptor{ ctx, { address, port } };
    boost::asio::ip::tcp::socket socket{ ctx };

    http_connection::worker_pool_t pool(2);
    http_server(acceptor, socket, pool);

    ctx.run();
  } catch (std::exception const& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}