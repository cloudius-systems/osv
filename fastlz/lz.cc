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

// Compress the Kernel using fastlz algorithm
int main(int argc, char* argv[])
{
    size_t input_length, output_length;
    char *input, *output;

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

    output = new char[(int)(input_length*2)];
    output_length = fastlz_compress(input, input_length, output);

    ofstream output_file((std::string(argv[1]) + ".lz").c_str(), ios::out|ios::binary|ios::trunc);

    if (output_file) {
        output_file.write(output, output_length);
        output_file.close();
    }
    else {
        cout << "Error opening output file\n";
        return EXIT_FAILURE;
    }

    delete[] input;
    delete[] output;

    return 0;
}
