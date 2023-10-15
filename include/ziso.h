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
#include <cstring>
#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>

// The LZ4_ACCELERATION_MAX is defined in the lz4.c file and is about 65537 (now).
// Testing I have noticed that above 1024 the compression was almost the same, so I'll set the max there.
#define LZ4_MAX_ACCELERATION 1024
uint16_t lz4_compression_level[12] = {
    LZ4_MAX_ACCELERATION,
    uint16_t(LZ4_MAX_ACCELERATION *((float)10 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)9 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)8 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)7 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)6 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)5 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)4 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)3 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)2 / 11)),
    uint16_t(LZ4_MAX_ACCELERATION *((float)1 / 11)),
    1};

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
    bool compress = true;
    uint32_t blockSize = 2048;
    uint8_t compressionLevel = 12;
    bool alternativeLz4 = false;
    bool bruteForce = false;
    bool lz4hc = false;
    bool overwrite = false;
    bool keepOutput = false;
} opt_struct;

struct summary
{
    uint64_t sourceSize = 0;
    uint64_t lz4Count = 0;
    uint64_t lz4In = 0;
    uint64_t lz4Out = 0;
    uint64_t lz4m2Count = 0;
    uint64_t lz4m2In = 0;
    uint64_t lz4m2Out = 0;
    uint64_t lz4hcCount = 0;
    uint64_t lz4hcIn = 0;
    uint64_t lz4hcOut = 0;
    uint64_t rawCount = 0;
    uint64_t raw = 0;
} summary_struct;

// MB Macro
#define MB(x) ((float)(x) / 1024 / 1024)

///////////////////////////////
//
// Functions
//
/**
 * @brief Compress a block
 *
 * @param src The source data to "compress" (or not)
 * @param srcSize The source data size
 * @param dst The destination buffer to store the data. It must have enough space or will fail
 * @param dstSize The space in the destination buffer
 * @param uncompressed (output) True if the data was not compressed or false otherwise.
 * @param options Program options
 * @return uint32_t The compressed data size. Will return 0 if something was wrong.
 */
uint32_t compress_block(
    const char *src,
    uint32_t srcSize,
    char *dst,
    uint32_t dstSize,
    bool &uncompressed,
    opt options);

uint32_t decompress_block(
    const char *src,
    uint32_t srcSize,
    char *dst,
    uint32_t dstSize,
    bool uncompressed);

void file_align(
    std::fstream &fOut,
    uint8_t shift);

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

static void progress_compress(uint64_t currentInput, uint64_t totalInput, uint64_t currentOutput);
static void progress_decompress(uint64_t currentInput, uint64_t totalInput);
static void show_summary(uint64_t outputSize, opt options);