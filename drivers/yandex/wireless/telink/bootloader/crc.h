#pragma once

unsigned short tlCrc16(const unsigned char* buf, int len, unsigned short crc);
unsigned int tlCrc32(const unsigned char* buf, int len, unsigned int crc);
