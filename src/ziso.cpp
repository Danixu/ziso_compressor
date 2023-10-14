#include "ziso.h"
#include "../lib/lz4/lib/lz4.h"
#include "../lib/lz4/lib/lz4hc.h"

static struct option long_options[] = {
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"compression", required_argument, NULL, 'c'},
    {"block-size", required_argument, NULL, 'b'},
    {"force", required_argument, NULL, 'f'},
    {"keep-output", required_argument, NULL, 'k'},
    {"help", required_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

int main(int argc, char **argv)
{
    // Start the timer to measure execution time
    auto start = std::chrono::high_resolution_clock::now();

    // Return code.
    int return_code = 0;

    // Main options
    opt settings;

    // Header
    zheader fileHeader;

    // Other Variables
    uint64_t inputSize;
    uint32_t blocksNumber;
    uint16_t headerSize;
    char *fileHeaderC = nullptr;
    uint64_t blockStartPosition = 0;
    uint64_t blockRealStartPosition = 0;

    // Buffers
    std::vector<char> readBuffer;
    std::vector<char> writeBuffer;
    // Blocks data
    std::vector<uint32_t> blocks;

    // Input and output files
    std::fstream inFile;
    std::fstream outFile;

    return_code = get_options(argc, argv, settings);
    if (return_code)
    {
        goto exit;
    }

    if (settings.inputFile.empty())
    {
        fprintf(stderr, "ERROR: input file is required.\n");
        print_help();
        return 1;
    }

    // Open the input file
    inFile.open(settings.inputFile.c_str(), std::ios::in | std::ios::binary);
    // Tricky way to check if was oppened correctly.
    // The "is_open" method was failing on cross compiled EXE
    {
        char dummy;
        if (!inFile.read(&dummy, 0))
        {
            fprintf(stderr, "ERROR: input file cannot be opened.\n");
            return 1;
        }
    }

    outFile.open("test.zso", std::ios::out | std::ios::binary | std::ios::trunc);

    // Get the input size
    inFile.seekg(0, std::ios_base::end);
    inputSize = inFile.tellg();
    inFile.seekg(0, std::ios_base::beg);

    // Get the total blocks
    blocksNumber = ceil((float)inputSize / settings.blockSize) + 1;
    // Calculate the header size
    headerSize = 0x18 + (blocksNumber * sizeof(uint32_t));

    // Set the header input size and block size
    fileHeader.uncompressedSize = inputSize;
    fileHeader.blockSize = settings.blockSize;

    // Set shift depending of the input size. Bigger shift means more waste.
    if (inputSize > (0x3FFFFFFFF - headerSize))
    {
        // Size is bigger than 17.179.869.183 (16GB-32GB). PS2 games are that big.
        fileHeader.indexShift = 4;
    }
    if (inputSize > (0x1FFFFFFFF - headerSize))
    {
        // Size is bigger than 8.589.934.591 (8GB-16GB)
        fileHeader.indexShift = 3;
    }
    else if (inputSize > (0xFFFFFFFF - headerSize))
    {
        // Size is bigger than 4.294.967.295 (4GB-8GB)
        fileHeader.indexShift = 2;
    }
    else if (inputSize > (0x7FFFFFFF - headerSize))
    {
        // Size is bigger than 2.147.483.647 (2GB-4GB)
        fileHeader.indexShift = 1;
    }
    // Files with less than 2GB doesn't need to shift.

    // Convert and write the header
    fileHeaderC = new char(sizeof(fileHeader));
    memcpy(fileHeaderC, &fileHeader, sizeof(fileHeader));

    outFile.write(fileHeaderC, sizeof(fileHeader));
    delete[] fileHeaderC;

    // Reserve the blocks index space
    blocks.resize(blocksNumber, 0);

    outFile.write((const char *)blocks.data(), blocksNumber * sizeof(uint32_t));

    readBuffer.resize(settings.blockSize, 0);
    writeBuffer.resize(settings.blockSize, 0);

    for (uint32_t currentBlock = 0; currentBlock < blocksNumber - 1; currentBlock++)
    {
        uint64_t blockStartPosition = outFile.tellp();
        uint64_t blockRealStartPosition = blockStartPosition;

        uint64_t toRead = settings.blockSize;
        uint64_t leftInFile = inputSize - inFile.tellg();
        if (leftInFile < toRead)
        {
            toRead = leftInFile;
        }

        inFile.read(readBuffer.data(), toRead);

        int compressedBytes = LZ4_compress_fast(readBuffer.data(), writeBuffer.data(), toRead, writeBuffer.size(), 1024);

        if (compressedBytes > 0)
        {
            outFile.write(writeBuffer.data(), compressedBytes);
        }
        else
        {
            outFile.write(readBuffer.data(), settings.blockSize);
        }

        blocks[currentBlock] = pos_to_index(blockRealStartPosition, fileHeader.indexShift, compressedBytes == 0);

        if (blockRealStartPosition > blockStartPosition)
        {
            for (uint64_t i = blockStartPosition; i <= blockRealStartPosition; i++)
            {
                outFile.write("\0", 1);
            }
        }
    }

    // Set the eof position block
    blockStartPosition = outFile.tellp();
    blockRealStartPosition = blockStartPosition;
    blocks[blocksNumber - 1] = pos_to_index(blockRealStartPosition, fileHeader.indexShift, false);

    if (blockRealStartPosition > blockStartPosition)
    {
        for (uint64_t i = blockStartPosition; i <= blockRealStartPosition; i++)
        {
            fprintf(stdout, "Syncing, position...");
            outFile.write("\0", 1);
        }
    }

    // Write the blocks index
    outFile.seekp(0x18);
    outFile.write((const char *)blocks.data(), blocksNumber * sizeof(uint32_t));

exit:
    if (inFile.is_open())
    {
        inFile.close();
    }
    if (outFile.is_open())
    {
        outFile.close();
    }

    if (return_code == 0)
    {
        auto stop = std::chrono::high_resolution_clock::now();
        auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
        fprintf(stdout, "\n\nThe file was processed without any problem\n");
        fprintf(stdout, "Total execution time: %0.3fs\n\n", executionTime.count() / 1000.0F);
    }
    else
    {
        if (!settings.keepOutput)
        {
            // Something went wrong, so output file must be deleted if keep == false
            // We will remove the file if something went wrong
            fprintf(stderr, "\n\nERROR: there was an error processing the input file.\n\n");
            std::ifstream out_remove_tmp(settings.outputFile.c_str(), std::ios::binary);
            char dummy;
            if (out_remove_tmp.read(&dummy, 0))
            {
                out_remove_tmp.close();
                if (remove(settings.outputFile.c_str()))
                {
                    fprintf(stderr, "There was an error removing the output file... Please remove it manually.\n");
                }
            }
        }
    }
    return return_code;
}

uint32_t compress_block(char *dst, uint32_t dstSize, const char *src, uint32_t srcSize, bool &compressed, opt settings)
{
    // Try to compress the data into the dst buffer
    uint32_t outSize = 0;
    if (settings.lz4hc)
    {
        // LZ4_compressHC_continue();
    }
}

uint32_t pos_to_index(uint64_t &filePosition, uint8_t shift, bool uncompressed)
{
    // Shift right the required bits and store the position into the uint32_t variable
    uint32_t indexPosition = filePosition >> shift;
    // Set the "new" file position
    uint64_t newFilePosition = indexPosition << shift;
    // Check if the new position is lower than the original position a padding will be applied. The difference must be padded using zeroes in the output file.
    if (filePosition > newFilePosition)
    {
        indexPosition++;
        newFilePosition = indexPosition << shift;
    }
    // Set the compression bit
    indexPosition = indexPosition | uncompressed << 31;

    // Return the filePosition variable
    filePosition = newFilePosition;

    // Return the index position
    return indexPosition;
}

uint64_t index_to_pos(uint32_t indexData, uint8_t shift, bool &uncompressed)
{
    uncompressed = indexData & 0x80000000;
    return (indexData & 0x7FFFFFFF) << shift;
}

int get_options(
    int argc,
    char **argv,
    opt &options)
{
    char ch;
    // temporal variables for options parsing
    uint64_t temp_argument = 0;

    std::string optarg_s;

    while ((ch = getopt_long(argc, argv, "i:o:c:b:fkh", long_options, NULL)) != -1)
    {
        // check to see if a single character or long option came through
        switch (ch)
        {
        // short option '-i', long option '--input'
        case 'i':
            options.inputFile = optarg;
            break;

        // short option '-o', long option "--output"
        case 'o':
            options.outputFile = optarg;
            break;

        // short option '-c', long option "--compression"
        // Compression level
        case 'c':
            try
            {
                optarg_s = optarg;
                temp_argument = std::stoi(optarg_s);

                if (temp_argument < 1 || temp_argument > 9)
                {
                    fprintf(stderr, "ERROR: the provided compression level option is not correct.\n\n");
                    print_help();
                    return 1;
                }
                else
                {
                    options.compressionLevel = (uint8_t)temp_argument;
                }
            }
            catch (std::exception const &e)
            {
                fprintf(stderr, "ERROR: the provided compression level is not correct.\n\n");
                print_help();
                return 1;
            }
            break;

        // short option '-b', long option "--block-size"
        case 'b':
            try
            {
                optarg_s = optarg;
                temp_argument = std::stoi(optarg_s);

                if (!temp_argument || temp_argument < 512)
                {
                    fprintf(stderr, "ERROR: the provided block size is not correct. Must be at least 512\n\n");
                    print_help();
                    return 1;
                }
                else
                {
                    options.blockSize = (uint8_t)temp_argument;
                }
            }
            catch (std::exception const &e)
            {
                fprintf(stderr, "ERROR: the provided block size is not correct.\n\n");
                print_help();
                return 1;
            }
            break;

        // short option '-f', long option "--force"
        case 'f':
            options.overwrite = true;
            break;

        // short option '-k', long option "--keep-output"
        case 'k':
            options.keepOutput = true;
            break;

        case 'h':
        case '?':
            print_help();
            return 0;
            break;
        }
    }

    return 0;
}

void print_help()
{
    banner();
    fprintf(stderr,
            "Usage:\n"
            "\n"
            "The program detects ziso sources and selects the decompression mode:\n"
            "    ecmtool -i/--input example.iso\n"
            "    ecmtool -i/--input example.iso -o/--output example.zso\n"
            "    ecmtool -i/--input example.zso\n"
            "    ecmtool -i/--input example.zso -o/--output example.iso\n"
            "Optional options:\n"
            "    -c/--compression 1-9\n"
            "           Compression level to be used. By default 9.\n"
            "    -b/--block-size <size>\n"
            "           The size in bytes of the blocks. By default 2048.\n"
            "    -f/--force\n"
            "           Force to ovewrite the output file\n"
            "    -k/--keep-output\n"
            "           Keep the output when something went wrong, otherwise will be removed on error.\n"
            "    -h/--help\n"
            "           Show this help message.\n"
            "\n");
}