/*******************************************************************************
 */
#define TITLE "ziso - ZSO compressor/decompressor"
#define COPYR "Created by Daniel Carrasco (2023)"
#define VERSI "0.1.0"
/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include "banner.h"
#include <chrono>
#include <getopt.h>
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <vector>

// The LZ4_ACCELERATION_MAX is defined in the lz4.c file and is about 65537 (now).
// Testing I have noticed that above 1024 the compression was almost the same, so I'll set the max there.
#define LZ4_MAX_ACCELERATION 1024

#pragma pack(push)
#pragma pack(1)
struct zheader
{
    char magic[4] = {'Z', 'I', 'S', 'O'}; // Always "ZISO".
    uint32_t headerSize = 0x18;           // Always 0x18.
    uint64_t uncompressedSize = 0;        // Total size of original ISO.
    uint32_t blockSize = 2048;            // Size of each block, usually 2048.
    uint8_t version = 1;                  // Always 1.
    uint8_t indexShift = 0;               // Indicates left shift of index values.
    uint8_t unused[2] = {0, 0};           // Always 0.
} ziso_header;
#pragma pack(pop)

struct opt
{
    std::string inputFile = "";
    std::string outputFile = "";
    uint32_t blockSize = 2048;
    uint8_t compressionLevel = 9;
    bool lz4hc = false;
    bool overwrite = false;
    bool keepOutput = false;
} opt_struct;

///////////////////////////////
//
// Functions
//
/**
 * @brief Compress a block
 *
 * @param dst The destination buffer to store the data. It must have enough space or will fail
 * @param dstSize The space in the destination buffer
 * @param src The source data to "compress" (or not)
 * @param srcSize The source data size
 * @param compressed (output) True if the data was compressed or false if data is raw
 * @param settings Program settings
 * @return uint32_t The compressed data size. Will return 0 if something was wrong.
 */
uint32_t compress_block(
    char *dst,
    uint32_t dstSize,
    const char *src,
    uint32_t srcSize,
    bool &compressed,
    opt settings);

/**
 * @brief Convert a file position to index position considering the shift data
 *
 * @param filePosition (input/output) Start point of the block. Will be updated with the new position if shift is used.
 * @param shift Bits to shift the filePosition variable
 * @param compressed The block is a compressed block
 * @return uint32_t The Index data that will be stored into the zso file.
 */
uint32_t pos_to_index(
    uint64_t &filePosition,
    uint8_t shift,
    bool uncompressed);

/**
 * @brief Convert the index position to the file position considering the shift data.
 *
 * @param indexData The index entry in uint32_t.
 * @param shift Bits that the position was shifted
 * @param compressed (output) Returns the compression status of the index entry.
 * @return uint64_t The position of the block
 */
uint64_t index_to_pos(uint32_t indexData, uint8_t shift, bool &uncompressed);

/**
 * @brief Prints the help message
 *
 */
void print_help();

/**
 * @brief Get the options object
 *
 * @param argc
 * @param argv
 * @param options
 * @return int
 */
int get_options(
    int argc,
    char **argv,
    opt &options);