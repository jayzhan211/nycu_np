//
// socks4.hpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2015 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// https://www.boost.org/doc/libs/1_60_0/doc/html/boost_asio/example/cpp03/socks4/socks4.hpp

#pragma once

#ifndef SOCKS4_HPP
#define SOCKS4_HPP

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <string>

#include "constant.hpp"

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace socks4 {

class Request {
public:
  enum command_type { connect = 0x01, bind = 0x02 };
  unsigned char version_;
  unsigned char command_;
  unsigned char port_high_byte_;
  unsigned char port_low_byte_;
  uint32_t address_;
  
  std::string user_id_;
  std::string domain_name_;
  
  bool isSocks4a;

  boost::asio::ip::address_v4::bytes_type address_v4;
  unsigned char null_byte_;

  explicit Request() {}
  
  explicit Request(command_type command,  tcp::endpoint endpoint) : 
  version_(socks4::constant::VERSION), command_(command), null_byte_(0x00) {
    auto port = endpoint.port();
    port_high_byte_ = (port >> 8) & 0xff;
    port_low_byte_ = port & 0xff;
    address_v4 = endpoint.address().to_v4().to_bytes();
  }

  explicit Request(std::vector<unsigned char> buffer, std::size_t bytes) : isSocks4a(false) {
    // TODO: error handling
    version_ = buffer[0];
    command_ = buffer[1];
    port_high_byte_ = buffer[2];
    port_low_byte_ = buffer[3];
    for (size_t i = 4; i < 8; i++) {
      address_ = (address_ << 8) | buffer[i];
    }
    size_t i = 8;
    while (i < bytes && buffer[i] != 0) {
      user_id_ += static_cast<char>(buffer[i]);
      i++;
    }

    i = 9;

    if (address_ > 0 && address_ <= std::numeric_limits<uint8_t>::max() &&
        i + 1 < bytes) {
      isSocks4a = true;

      while (i < bytes && buffer[i] != 0) {
        domain_name_ += static_cast<char>(buffer[i]);
        i++;
      }
    }
  }

  boost::asio::ip::tcp::endpoint get_endpoint() {
    return boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4(address_),
                                          get_port());
  }

  socks4::constant::SOCKS_TYPE get_command_type() {
    if(command_ == connect) return socks4::constant::SOCKS_TYPE::CONNECT;
    if(command_ == bind) return socks4::constant::SOCKS_TYPE::BIND;
    return socks4::constant::SOCKS_TYPE::UNKNOWN;
  }

  boost::array<boost::asio::mutable_buffer, socks4::constant::REQUEST_INDEX> buffers() {
    return {boost::asio::buffer(&version_, 1),
            boost::asio::buffer(&command_, 1),
            boost::asio::buffer(&port_high_byte_, 1),
            boost::asio::buffer(&port_low_byte_, 1),
            boost::asio::buffer(address_v4),
            boost::asio::buffer(user_id_),
            boost::asio::buffer(&null_byte_, 1)};
  }

  std::string get_command() {
    if (command_ == command_type::connect)
      return "CONNECT";
    if (command_ == command_type::bind)
      return "BIND";
    return "UNKNOWN";
  }

  uint16_t get_port() { 
    uint16_t port = static_cast<uint16_t>(port_high_byte_);
    port = (port << 8) & 0xff00;
    port |= port_low_byte_;
    return port;
  }

  std::string get_address() {
    return boost::asio::ip::address_v4(address_).to_string();
  }
};

class reply {
public:
  enum statusType { Accept = 0x5a, Reject = 0x5b };

  reply() {}

  boost::array<net::mutable_buffer, 5> buffers() {
    return {net::buffer(&version_, 1), net::buffer(&status_, 1),
            net::buffer(&destPortHigh_, 1), net::buffer(&destPortLow_, 1),
            net::buffer(destIP_)};
  }

private:
  unsigned char version_;
  unsigned char status_;
  unsigned char destPortHigh_;
  unsigned char destPortLow_;
  boost::asio::ip::address_v4::bytes_type destIP_{};
};

} // namespace socks4

#endif // SOCKS4_HPP