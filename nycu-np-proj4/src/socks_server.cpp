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

#include <boost/beast.hpp>  // request_parser
#include <boost/format.hpp> // boost::format
#include <stdlib.h>
#include <sys/wait.h> // wait()

#include "constant.hpp"
#include "firewall.hpp"
#include "socks4.hpp"

using boost::asio::buffer;
using boost::asio::ip::tcp;
using boost::beast::http::request_parser;
using boost::beast::http::string_body;
using namespace std;

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace net = boost::asio;    // from <boost/asio.hpp>

// Report a failure
void fail(beast::error_code ec, char const *what)
{
  std::cerr << what << ": " << ec.message() << "\n";
}

class session : public std::enable_shared_from_this<session>
{
public:
  // Objects are constructed with a strand to
  // ensure that handlers do not execute concurrently.
  explicit session(net::io_context &io_context, tcp::socket socket)
      : client_(std::move(socket)), server_(io_context), resolver_(io_context),
        acceptor_(io_context, tcp::endpoint(tcp::v4(), 0))
  {
    request_ = socks4::Request();
    socks4_buffer.resize(socks4::constant::MAX_BUFFER_SIZE);
    client_buffer.resize(socks4::constant::MAX_BUFFER_SIZE);
    server_buffer.resize(socks4::constant::MAX_BUFFER_SIZE);
  }

  // Start the asynchronous operation

  void start() { receive_socks4_request(); }

private:
  void log(string source_ip, string source_port, string destination_ip,
           string destination_port, string command, string reply)
  {
    string fmt = "<S_IP>: %1%\n"
                 "<S_PORT>: %2%\n"
                 "<D_IP>: %3%\n"
                 "<D_PORT>: %4%\n"
                 "<Command>: %5%\n"
                 "<Reply>: %6%\n";

    cout << boost::format(fmt) % source_ip % source_port % destination_ip %
                destination_port % command % reply;
  }

  void async_write_to_client(std::size_t length)
  {
    auto self(shared_from_this());
    client_.async_write_some(
        net::buffer(server_buffer, length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            async_read_from_server();
          }
          else if (ec == boost::asio::error::eof)
          {
            boost::system::error_code ignored_ec;
            client_.shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                             ignored_ec);
          }
          else
          {
            return fail(ec, "async_write_to_server");
          }
        });
  }

  void async_read_from_server()
  {
    // TODO: difference between read some?
    auto self(shared_from_this());
    server_.async_receive(
        net::buffer(server_buffer),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            async_write_to_client(length);
          }
          else if (ec == boost::asio::error::eof)
          {
            boost::system::error_code ignored_ec;
            server_.shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                             ignored_ec);
          }
          else
          {
            return fail(ec, "async_write_to_server");
          }
        });
  }

  void async_write_to_server(std::size_t length)
  {
    auto self(shared_from_this());
    server_.async_write_some(
        net::buffer(client_buffer, length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            async_read_from_client();
          }
          else if (ec == boost::asio::error::eof)
          {
            boost::system::error_code ignored_ec;
            server_.shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                             ignored_ec);
          }
          else
          {
            return fail(ec, "async_write_to_server");
          }
        });
  }

  void async_read_from_client()
  {
    auto self(shared_from_this());
    client_.async_read_some(
        net::buffer(client_buffer),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            async_write_to_server(length);
          }
          else if (ec == boost::asio::error::eof)
          {
            boost::system::error_code ignored_ec;
            client_.shutdown(boost::asio::ip::tcp::socket::shutdown_both,
                             ignored_ec);
          }
          else
          {
            return fail(ec, "async_read_from_client");
          }
        });
  }

  void do_reply(uint8_t code, uint16_t port = 0, boost::asio::ip::address_v4::bytes_type address = array<unsigned char, 4>())
  {
    auto self(shared_from_this());
    vector<unsigned char> packet(8, 0x00);
    packet[1] = code;
    packet[2] = (port >> 8) & 0xff;
    packet[3] = port & 0xff;
    for (size_t i = 4; i < 8; i++)
      packet[i] = address[i - 4];

    client_.async_write_some(
        net::buffer(packet, 8),
        [this, self](boost::system::error_code ec, std::size_t bytes)
        {
          if (ec)
            return fail(ec, "do_reply");
        });
  }

  void do_connect(tcp::endpoint endpoint)
  {

    if (!Firewall::check(endpoint.address().to_string(),
                         request_.get_command_type()))
    {
      do_reply(socks4::constant::STATUS_TYPE::REJECT);
      return;
    }

    auto self(shared_from_this());
    if (request_.command_ == socks4::constant::SOCKS_TYPE::CONNECT)
    {
      server_.async_connect(endpoint,
                            [this, self](const boost::system::error_code &ec)
                            {
                              if (ec)
                              {
                                do_reply(socks4::constant::STATUS_TYPE::REJECT);
                                return fail(ec, "do_connect");
                              }

                              do_reply(socks4::constant::STATUS_TYPE::ACCEPT);
                              async_read_from_server();
                              async_read_from_client();
                            });
    }
    else if (request_.command_ == socks4::constant::SOCKS_TYPE::BIND)
    {
      auto address = acceptor_.local_endpoint().address();
      if (acceptor_.is_open())
      {
        auto port = acceptor_.local_endpoint().port();
        // cout << "address: " << address.to_string() << " "
        //      << "port: " << port << endl;
        do_reply(socks4::constant::STATUS_TYPE::ACCEPT, port, address.to_v4().to_bytes());
      }
      else
      {
        do_reply(socks4::constant::STATUS_TYPE::REJECT);
      }
      acceptor_.async_accept(
          server_, [this, self, endpoint](boost::system::error_code ec)
          {
            if (ec)
            {
              do_reply(socks4::constant::STATUS_TYPE::REJECT);
              return fail(ec, "do_accept");
            }
            else
            {
              auto remote_address = server_.remote_endpoint().address(); // boost::asio::ip::address
              auto port = acceptor_.local_endpoint().port();
              if (acceptor_.is_open())
              {
                // cout << "remote address: " << remote_address << " "
                //      << "port: " << port << endl;
                do_reply(socks4::constant::STATUS_TYPE::ACCEPT, port, remote_address.to_v4().to_bytes());
                async_read_from_server();
                async_read_from_client();
              }
              else
              {
                do_reply(socks4::constant::STATUS_TYPE::REJECT);
              }
            }
          });
    }
  }

  void do_resolve()
  {
    auto host = request_.get_address();
    auto service_name = to_string(request_.get_port());
    if (request_.isSocks4a)
    {
      host = request_.domain_name_;
    }
    auto self(shared_from_this());
    resolver_.async_resolve(
        host, service_name,
        [this, self](const boost::system::error_code &error,
                     boost::asio::ip::tcp::resolver::results_type results)
        {
          if (error == boost::asio::error::eof)
          {
          }
          else if (error)
            fail(error, "do_resolve()");
          else
          {

            for (auto r : results)
            {
              auto addr = r.endpoint().address();

              if (addr.is_v4())
              {
                do_connect(r.endpoint());
              }
            }
          }
        });
  }

  // receive socks4 request
  void receive_socks4_request()
  {
    auto self(shared_from_this());
    client_.async_read_some(
        net::buffer(socks4_buffer),
        [this, self](boost::system::error_code ec, std::size_t bytes)
        {
          if (ec == boost::asio::error::eof)
          {
          }

          else if (ec)
            return fail(ec, "receive_socks4_request");
          else
          {
            request_ = socks4::Request(socks4_buffer, bytes);

            string remote_address =
                client_.remote_endpoint().address().to_string();
            string remote_port = to_string(client_.remote_endpoint().port());

            string address = request_.get_address();
            string port = to_string(request_.get_port());
            string command = request_.get_command();

            // TODO: When do we reply "reject"
            log(remote_address, remote_port, address, port, command, "Accept");
            do_resolve();
          }
        });
  }

  tcp::socket client_;
  tcp::socket server_;

  tcp::acceptor acceptor_;

  tcp::resolver resolver_;

  socks4::Request request_;

  std::vector<unsigned char> socks4_buffer;
  std::vector<unsigned char> server_buffer;
  std::vector<unsigned char> client_buffer;
};

class server
{
public:
  server(boost::asio::io_context &io_context, uint16_t port)
      : io_context_(io_context), signal_(io_context, SIGCHLD),
        acceptor_(io_context, {tcp::v4(), port}), socket_(io_context)
  {
    wait_for_signal();
    do_accept();
  }

private:
  void wait_for_signal()
  {
    signal_.async_wait([this](boost::system::error_code /*ec*/, int /*signo*/)
                       {
                         // Only the parent process should check for this signal. We can
                         // determine whether we are in the parent by checking if the acceptor
                         // is still open.
                         if (acceptor_.is_open())
                         {
                           // Reap completed child processes so that we don't end up with
                           // zombies.
                           int status = 0;
                           while (waitpid(-1, &status, WNOHANG) > 0)
                           {
                           }

                           wait_for_signal();
                         }
                       });
  }
  void do_accept()
  {
    acceptor_.async_accept([this](boost::system::error_code ec,
                                  tcp::socket new_socket)
                           {
                             if (!ec)
                             {
                               // Take ownership of the newly accepted socket.
                               socket_ = std::move(new_socket);

                               // Inform the io_context that we are about to fork. The io_context
                               // cleans up any internal resources, such as threads, that may
                               // interfere with forking.
                               io_context_.notify_fork(boost::asio::io_context::fork_prepare);

                               if (fork() == 0)
                               {
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

                                 std::make_shared<session>(io_context_, std::move(socket_))->start();
                               }
                               else
                               {
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
                             }
                             else
                             {
                               std::cerr << "Accept error: " << ec.message() << std::endl;
                               do_accept();
                             }
                           });
  }

  boost::asio::io_context &io_context_;
  boost::asio::signal_set signal_;
  tcp::acceptor acceptor_;
  tcp::socket socket_;
};

int main(int argc, char *argv[])
{

  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}