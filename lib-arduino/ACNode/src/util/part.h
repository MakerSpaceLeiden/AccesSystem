#ifndef _H_PARTPRINT
#define _H_PARTPRINT

#include <Arduino.h>

void partition_info(Print &out);
String currentPartition();
size_t get_rom_size();
#endif



