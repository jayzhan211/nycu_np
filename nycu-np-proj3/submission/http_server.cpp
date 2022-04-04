//
// async_tcp_echo_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/asio.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>

#include <boost/beast.hpp> // request_parser
#include <stdlib.h>
#include <sys/wait.h> // wait()

using boost::asio::buffer;
using boost::asio::ip::tcp;
using boost::beast::http::request_parser;
using boost::beast::http::string_body;
using namespace std;

class session : public std::enable_shared_from_this<session> {
public:
  session(tcp::socket socket) : socket_(std::move(socket)) {}

  void start() { do_read(); }

private:
  void do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(
        boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length) {
          request_parser<string_body> parser;

          if (!ec) {
            parser.put(buffer(data_), ec);
            auto res = parser.get();
            setenv("REQUEST_METHOD", res.method_string().to_string().c_str(),
                   1);

            auto target = res.target().to_string();

            auto index = target.find_first_of('?');
            auto uri =
                (index == string::npos) ? target : target.substr(0, index);
            auto query_string =
                (index == string::npos) ? "" : target.substr(index + 1);

            setenv("REQUEST_URI", uri.c_str(), 1);
            setenv("QUERY_STRING", query_string.c_str(), 1);

            auto version = res.version();
            auto get_http_protocal = [](int version) {
              int major = version / 10;
              int minor = version %= 10;
              return "HTTP/" + to_string(major) + "." + to_string(minor);
            };
            auto server_protocal = get_http_protocal(version);
            setenv("SERVER_PROTOCOL", server_protocal.c_str(), 1);

            string http_host = "";
            try {
              http_host = res.at("Host").to_string();
            } catch (const std::exception &e) {
              cout << "Host: " << e.what() << endl;
            }
            setenv("HTTP_HOST", http_host.c_str(), 1);

            string server_address =
                socket_.local_endpoint().address().to_string();
            string server_port = to_string(socket_.local_endpoint().port());
            setenv("SERVER_ADDR", server_address.c_str(), 1);
            setenv("SERVER_PORT", server_port.c_str(), 1);

            string remote_address =
                socket_.remote_endpoint().address().to_string();
            string remote_port = to_string(socket_.remote_endpoint().port());
            setenv("REMOTE_ADDR", server_address.c_str(), 1);
            setenv("REMOTE_PORT", server_port.c_str(), 1);

            // move path to current dir
            uri.insert(uri.begin(), '.');

            dup2(socket_.native_handle(), 0);
            dup2(socket_.native_handle(), 1);
            dup2(socket_.native_handle(), 2);

            cout << server_protocal << " " << 200 << " "
                 << "OK" << endl;

            char *argv[] = {nullptr};
            if (execv(uri.c_str(), argv) == -1) {
              cout << server_protocal << " " << 404 << " "
                   << "Not Found" << endl;
              cout << "Content-type: text/plain" << endl << endl;
              cout << uri << ": No such file or directory" << endl;
              exit(-1);
            }
          }
        });
  }

  void do_write(std::size_t length) {
    auto self(shared_from_this());
    boost::asio::async_write(
        socket_, boost::asio::buffer(data_, length),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
          if (!ec) {
            do_read();
          }
        });
  }

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
};

class server {
public:
  server(boost::asio::io_context &io_context, uint16_t port)
      : io_context_(io_context), signal_(io_context, SIGCHLD),
        acceptor_(io_context, {tcp::v4(), port}), socket_(io_context) {
    wait_for_signal();
    do_accept();
  }

private:
  void wait_for_signal() {
    signal_.async_wait([this](boost::system::error_code /*ec*/, int /*signo*/) {
      // Only the parent process should check for this signal. We can
      // determine whether we are in the parent by checking if the acceptor
      // is still open.
      if (acceptor_.is_open()) {
        // Reap completed child processes so that we don't end up with
        // zombies.
        int status = 0;
        while (waitpid(-1, &status, WNOHANG) > 0) {
        }

        wait_for_signal();
      }
    });
  }
  void do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket new_socket) {
          if (!ec) {
            // Take ownership of the newly accepted socket.
            socket_ = std::move(new_socket);

            // Inform the io_context that we are about to fork. The io_context
            // cleans up any internal resources, such as threads, that may
            // interfere with forking.
            io_context_.notify_fork(boost::asio::io_context::fork_prepare);

            if (fork() == 0) {
              // Inform the io_context that the fork is finished and that this
              // is the child process. The io_context uses this opportunity to
              // create any internal file descriptors that must be private to
              // the new process.
              io_context_.notify_fork(boost::asio::io_context::fork_child);

              // The child won't be accepting new connections, so we can close
              // the acceptor. It remains open in the parent.
              acceptor_.close();

              // The child process is not interested in processing the SIGCHLD
              // signal.
              signal_.cancel();

              std::make_shared<session>(std::move(socket_))->start();
            } else {

              // Inform the io_context that the fork is finished (or failed)
              // and that this is the parent process. The io_context uses this
              // opportunity to recreate any internal resources that were
              // cleaned up during preparation for the fork.
              io_context_.notify_fork(boost::asio::io_context::fork_parent);

              // The parent process can now close the newly accepted socket. It
              // remains open in the child.
              socket_.close();

              do_accept();
            }

          } else {
            std::cerr << "Accept error: " << ec.message() << std::endl;
            do_accept();
          }
        });
  }
  void read() {
    socket_.async_read_some(
        boost::asio::buffer(data_),
        [this](boost::system::error_code ec, std::size_t length) {
          if (!ec)
            write(length);
        });
  }

  void write(std::size_t length) {
    boost::asio::async_write(
        socket_, boost::asio::buffer(data_, length),
        [this](boost::system::error_code ec, std::size_t /*length*/) {
          if (!ec)
            read();
        });
  }

  boost::asio::io_context &io_context_;
  boost::asio::signal_set signal_;
  tcp::acceptor acceptor_;
  tcp::socket socket_;
  std::array<char, 1024> data_;
};

int main(int argc, char *argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}