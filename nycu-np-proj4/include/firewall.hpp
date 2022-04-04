#pragma once

#include <string>

#include "constant.hpp"

using namespace std;

class Firewall {
public:
  Firewall();
  ~Firewall();
  static bool check(string address, socks4::constant::SOCKS_TYPE type);
};