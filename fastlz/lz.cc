/*
 * Copyright (C) 2014 Eduardo Piva
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "fastlz.h"
#include <iostream>
#include <fstream>
#include <cstdlib>

using namespace std;

// Compress the kernel by splitting it into smaller 1MB segments
// and compressing each one using fastlz algorithm and finally
// combining into single output file
//
// The resulting output file has following structure:
// - 4 bytes (int) field - offset where segment info table is stored
// - followed by each compressed segment content
// - followed by segment info table comprised of:
//    - 4 bytes (int) field - number of compressed segments
//    - followed by 8 bytes (2 int-wide fields) for each segment:
//       - uncompressed segment size in bytes
//       - compressed segment size in bytes
//
// If a compressed segment size is greater than MAX_COMPRESSED_SEGMENT_SIZE (~99% compression ratio)
// then original segment is placed into output.
int main(int argc, char* argv[])
{
    size_t input_length;
    char *input;

    if (argc != 2) {
        cout << "usage: lz inputfile\n";
        return EXIT_FAILURE;
    }

    ifstream input_file(argv[1], ios::in|ios::binary|ios::ate);

    if (input_file) {
        input_length = input_file.tellg();
        input = new char[input_length];

        input_file.seekg(0, ios::beg);
        input_file.read(input, input_length);
        input_file.close();
    }
    else {
        cout << "Error opening input file\n";
        return EXIT_FAILURE;
    }

    ofstream output_file((std::string(argv[1]) + ".lz").c_str(), ios::out|ios::binary|ios::trunc);
    if (!output_file) {
        cout << "Error opening output file\n";
        return EXIT_FAILURE;
    }
    // Leave space for offset of segment info table
    output_file.seekp(sizeof(int),ios::beg);

    int segments_count = input_length / SEGMENT_SIZE + 1;
    int *segment_sizes = new int[segments_count * 2];
    char *compressed_segment = new char[SEGMENT_SIZE * 2];
    //
    // Iterate over each 1MB chunk (or less for last chunk) of input data
    // and either append it compressed or original one as is
    for (auto segment = 0; segment < segments_count; segment++) {
        auto bytes_to_compress = (segment < segments_count - 1) ? SEGMENT_SIZE : input_length % SEGMENT_SIZE;
        segment_sizes[segment * 2] = bytes_to_compress;

        size_t compressed_segment_length =
                fastlz_compress(input + segment * SEGMENT_SIZE, bytes_to_compress, compressed_segment);
        //
        // Check if we actually compressed anything
        if (compressed_segment_length < bytes_to_compress && compressed_segment_length <= MAX_COMPRESSED_SEGMENT_SIZE) {
            output_file.write(compressed_segment, compressed_segment_length);
            segment_sizes[segment * 2 + 1] = compressed_segment_length;
        }
        // Otherwise write original uncompressed segment data (compression ratio would have been > 99%)
        else {
            output_file.write(input + segment * SEGMENT_SIZE, bytes_to_compress);
            segment_sizes[segment * 2 + 1] = bytes_to_compress;
        }
    }
    //
    // Write the segments info table
    int info_offset = output_file.tellp();
    output_file.write(reinterpret_cast<const char*>(&segments_count), sizeof(segments_count));
    output_file.write(reinterpret_cast<const char*>(segment_sizes), segments_count * 2 * sizeof(int));
    //
    // Write offset of the segments info table in the beginning of the file
    output_file.seekp(0,ios::beg);
    output_file.write(reinterpret_cast<const char*>(&info_offset), sizeof(info_offset));
    output_file.close();

    delete[] input;
    delete[] compressed_segment;

    return 0;
}
