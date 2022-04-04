#include "firewall.hpp"

#include <boost/algorithm/string/replace.hpp>
// #include <boost/filesystem.hpp>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <iostream>

Firewall::Firewall() {}
Firewall::~Firewall() {}
bool Firewall::check(string address, socks4::constant::SOCKS_TYPE type) {
  vector<string> allow_ip_for_connect;
  vector<string> allow_ip_for_bind;

  ifstream firewall_config("./socks.conf");
  string line;

  while (getline(firewall_config, line)) {
    stringstream ss(line);
    string _command, _type, _address;
    ss >> _command >> _type >> _address;

    if (_command != "permit")
      continue;
    boost::replace_all(_address, ".", "\\.");
    boost::replace_all(_address, "*", "\\d{1,3}");

    if (_type == "c") {
      allow_ip_for_connect.push_back(_address);
    } else if (_type == "b") {
      allow_ip_for_bind.push_back(_address);
    }
  }

  if (type == socks4::constant::SOCKS_TYPE::CONNECT) {
    for (auto address_regex : allow_ip_for_connect) {
      smatch string_match;

      if (regex_match(address, string_match, regex(address_regex)))
        return true;
    }
  } else if (type == socks4::constant::SOCKS_TYPE::BIND) {
    for (auto address_regex : allow_ip_for_bind) {
      smatch string_match;

      if (regex_match(address, string_match, regex(address_regex)))
        return true;
    }
  }
  return false;
}