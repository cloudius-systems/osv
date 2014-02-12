/*
  FastLZ - lightning-fast lossless compression library

  Copyright (C) 2007 Ariya Hidayat (ariya@kde.org)
  Copyright (C) 2006 Ariya Hidayat (ariya@kde.org)
  Copyright (C) 2005 Ariya Hidayat (ariya@kde.org)
*/

#ifndef FASTLZ_H
#define FASTLZ_H

#define FASTLZ_VERSION 0x000100

#define FASTLZ_VERSION_MAJOR     0
#define FASTLZ_VERSION_MINOR     0
#define FASTLZ_VERSION_REVISION  0

#define FASTLZ_VERSION_STRING "0.1.0"

/**
  Compress a block of data in the input buffer and returns the size of
  compressed block. The size of input buffer is specified by length. The
  minimum input buffer size is 16.

  The output buffer must be at least 5% larger than the input buffer
  and can not be smaller than 66 bytes.

  If the input is not compressible, the return value might be larger than
  length (input buffer size).

  The input buffer and the output buffer can not overlap.
*/

int fastlz_compress(const void* input, int length, void* output);

/**
  Decompress a block of compressed data and returns the size of the
  decompressed block. If error occurs, e.g. the compressed data is
  corrupted or the output buffer is not large enough, then 0 (zero)
  will be returned instead.

  The input buffer and the output buffer can not overlap.

  Decompression is memory safe and guaranteed not to write the output buffer
  more than what is specified in maxout.
 */

int fastlz_decompress(const void* input, int length, void* output, int maxout);

/**
  Compress a block of data in the input buffer and returns the size of
  compressed block. The size of input buffer is specified by length. The
  minimum input buffer size is 16.

  The output buffer must be at least 5% larger than the input buffer
  and can not be smaller than 66 bytes.

  If the input is not compressible, the return value might be larger than
  length (input buffer size).

  The input buffer and the output buffer can not overlap.

  Compression level can be specified in parameter level. At the moment,
  only level 1 and level 2 are supported.
  Level 1 is the fastest compression and generally useful for short data.
  Level 2 is slightly slower but it gives better compression ratio.

  Note that the compressed data, regardless of the level, can always be
  decompressed using the function fastlz_decompress above.
*/

int fastlz_compress_level(int level, const void* input, int length, void* output);

#endif /* FASTLZ_H */
