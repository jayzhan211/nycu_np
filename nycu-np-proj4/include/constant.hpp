#pragma once

#include <string>
using namespace std;

namespace socks4 {
namespace constant {
const int MAX_BUFFER_SIZE = 4096;
const unsigned char VERSION = 0x04;

const int REQUEST_INDEX = 7;

enum SOCKS_MODE { SOCKS4, SOCKS4A };
enum SOCKS_TYPE { CONNECT = 0x01, BIND = 0x02, UNKNOWN = 0x00};
enum STATUS_TYPE {
    ACCEPT = 0x5a,
    REJECT = 0x5b
  };
} // namespace constant
} // namespace socks4