#pragma once
#include <cstring>

struct BluetoothName {
  // Legacy scan responses allow 31 bytes, including the AD length and type.
  char value[30] = {};
  bool complete;

  explicit BluetoothName(const char *name = "Luxio") {
    if (!name || !*name) name = "Luxio";
    const size_t length = strlen(name);
    size_t count = length < sizeof(value) ? length : sizeof(value) - 1;
    // Keep UTF-8 characters intact when advertising a shortened local name.
    while (count < length && count && (static_cast<unsigned char>(name[count]) & 0xc0) == 0x80) count--;
    memcpy(value, name, count);
    complete = count == length;
  }
};
