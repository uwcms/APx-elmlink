#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

void crc32(const void *data, size_t n_bytes, uint32_t *crc);

#endif
