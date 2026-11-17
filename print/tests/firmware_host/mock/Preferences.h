#pragma once
#include <cstddef>
struct Preferences{ void begin(const char*,bool){} void end(){} size_t getBytesLength(const char*){return 0;} void getBytes(const char*,void*,size_t){} void putBytes(const char*,const void*,size_t){} };
