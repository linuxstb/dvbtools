#ifndef _FREESAT_HUFFMAN_H_
#define _FREESAT_HUFFMAN_H_

// POSIX header
#include <unistd.h>

void freesat_huffman_to_string(unsigned char* uncompressed, int outsize, const unsigned char *src, int size);

#endif // _FREESAT_HUFFMAN_H_
