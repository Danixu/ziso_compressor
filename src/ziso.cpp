#include "ziso.h"
#include "../lib/lz4/lib/lz4.h"
#include "../lib/lz4/lib/lz4hc.h"

static struct option long_options[] = {
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"compression", required_argument, NULL, 'c'},
    {"lz4hc", required_argument, NULL, 'l'},
    {"block-size", required_argument, NULL, 'b'},
    {"force", required_argument, NULL, 'f'},
    {"keep-output", required_argument, NULL, 'k'},
    {"help", required_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

uint8_t lastProgress = 100; // Force at 0% of progress
uint8_t lastRatio = 0;

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
    uint32_t headerSize;

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
        return_code = 1;
        goto exit;
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
            return_code = 1;
            goto exit;
        }
    }

    // Check if the file is an ECM3 File
    {
        char file_format[5] = {0};
        inFile.read(file_format, 4);

        if (
            file_format[0] == 'Z' &&
            file_format[1] == 'I' &&
            file_format[2] == 'S' &&
            file_format[3] == 'O')
        {
            // File is a ZISO file, so will be decompressed
            fprintf(stdout, "ZISO file detected. Decompressing...\n");
            settings.compress = false;
        }
        else
        {
            fprintf(stdout, "ISO file detected. Compressing to ZISO\n");
        }
    }

    // If no output filename was provided, generate it using the input filename
    if (settings.outputFile.empty())
    {
        // Remove the extensión
        std::string rawName = settings.inputFile.substr(0, settings.inputFile.find_last_of("."));

        // Input file will be decoded, so ecm2 extension must be removed (if exists)
        if (settings.compress)
        {
            settings.outputFile = rawName + ".zso";
        }
        else
        {
            settings.outputFile = rawName + ".iso";
        }
    }

    // Check if output file exists only if force_rewrite is false
    if (settings.overwrite == false)
    {
        char dummy;
        outFile.open(settings.outputFile.c_str(), std::ios::in | std::ios::binary);
        if (outFile.read(&dummy, 0))
        {
            fprintf(stderr, "ERROR: Cowardly refusing to replace output file. Use the -f/--force-rewrite options to force it.\n");
            settings.keepOutput = true;
            return_code = 1;
            goto exit;
        }
        outFile.close();
    }

    // Open the output file in replace mode
    outFile.open(settings.outputFile.c_str(), std::ios::out | std::ios::binary);
    // Check if file was oppened correctly.
    if (!outFile.good())
    {
        fprintf(stderr, "ERROR: output file cannot be opened.\n");
        return_code = 1;
        goto exit;
    }

    if (settings.compress)
    {
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

        // Print the sumary
        fprintf(stderr, "%20s %s\n", "Source:", settings.inputFile.c_str());
        fprintf(stderr, "%20s %s\n\n", "Destination:", settings.outputFile.c_str());
        fprintf(stderr, "%20s %lu bytes\n", "Total File Size:", inputSize);
        fprintf(stderr, "%20s %d\n", "Block Size:", settings.blockSize);
        fprintf(stderr, "%20s %d\n", "Index align:", fileHeader.indexShift);
        fprintf(stderr, "%20s %d\n", "Compress Level:", settings.compressionLevel);
        if (settings.lz4hc)
        {
            fprintf(stderr, "%20s Yes\n", "LZ4 HC Compression:");
            // 20
        }
        else
        {
            fprintf(stderr, "%20s %d\n", "LZ4 acceleration:", lz4_compression_level[settings.compressionLevel - 1]);
            fprintf(stderr, "%20s No\n", "LZ4 HC Compression:");
        }

        outFile.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));

        // Reserve the blocks index space
        blocks.resize(blocksNumber, 0);

        outFile.write((const char *)blocks.data(), blocksNumber * sizeof(uint32_t));

        readBuffer.resize(settings.blockSize, 0);
        writeBuffer.resize(settings.blockSize, 0);

        for (uint32_t currentBlock = 0; currentBlock < blocksNumber - 1; currentBlock++)
        {
            // Fill the output with zeroes until a valid start point depending of index shift
            file_align(outFile, fileHeader.indexShift);

            // Capture the block position
            uint64_t blockStartPosition = outFile.tellp();

            uint64_t toRead = settings.blockSize;
            uint64_t leftInFile = inputSize - inFile.tellg();
            if (leftInFile < toRead)
            {
                toRead = leftInFile;
            }

            inFile.read(readBuffer.data(), toRead);

            bool uncompressed = false;
            int compressedBytes = compress_block(
                readBuffer.data(),
                toRead,
                writeBuffer.data(),
                writeBuffer.size(),
                uncompressed,
                settings);

            if (compressedBytes > 0)
            {
                outFile.write(writeBuffer.data(), compressedBytes);
            }
            else
            {
                fprintf(stderr, "ERROR: There was an error compressing the source file.\n");
                return_code = 1;
                goto exit;
            }

            // Set the current block start point with the uncompressed flag
            blocks[currentBlock] = (blockStartPosition >> fileHeader.indexShift) | (uncompressed << 31);

            // Update the progress
            progress_compress(inFile.tellg(), inputSize, (uint64_t)outFile.tellp() - headerSize);
        }

        // Align the file and set the eof position block
        file_align(outFile, fileHeader.indexShift);
        uint64_t blockEndPosition = outFile.tellp();
        blocks[blocksNumber - 1] = (blockEndPosition >> fileHeader.indexShift);

        // Write the blocks index
        outFile.seekp(0x18);
        outFile.write((const char *)blocks.data(), blocksNumber * sizeof(uint32_t));
    }
    else
    {
        // Get the input size
        inFile.seekg(0, std::ios_base::end);
        inputSize = inFile.tellg();
        inFile.seekg(0, std::ios_base::beg);

        // Read the header
        inFile.read(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));

        // Calculate the blocks number
        blocksNumber = ceil(fileHeader.uncompressedSize / fileHeader.blockSize) + 1;

        // Reserve and read the blocks index
        blocks.resize(blocksNumber, 0);
        inFile.read((char *)blocks.data(), blocksNumber * sizeof(uint32_t));

        // Print the sumary
        fprintf(stderr, "%20s %s\n", "Source:", settings.inputFile.c_str());
        fprintf(stderr, "%20s %s\n\n", "Destination:", settings.outputFile.c_str());
        fprintf(stderr, "%20s %lu bytes\n", "Total File Size:", fileHeader.uncompressedSize);
        fprintf(stderr, "%20s %d\n", "Block Size:", fileHeader.blockSize);
        fprintf(stderr, "%20s %d\n", "Index align:", fileHeader.indexShift);

        // Check if the input file is damaged
        uint64_t headerFileSize = uint64_t(blocks[blocksNumber - 1] & 0x7FFFFFFF) << fileHeader.indexShift;

        if (headerFileSize != inputSize)
        {
            // The input file doesn't matches the index data and maybe is damaged
            fprintf(stderr, "\n\nERROR: The input file header is corrupt. Filesize doesn't matches.\n\n");
            return_code = 1;
            goto exit;
        }

        // Maybe not all the programs will try the best between compressed and uncompressed data
        // so reserve the double of read buffer space to be able to read >blockSize compressed blocks.
        readBuffer.resize(fileHeader.blockSize * 2, 0);
        writeBuffer.resize(fileHeader.blockSize, 0);

        for (uint32_t currentBlock = 0; currentBlock < blocksNumber - 1; currentBlock++)
        {
            bool uncompressed = blocks[currentBlock] & 0x80000000;
            uint64_t blockStartPosition = uint64_t(blocks[currentBlock] & 0x7FFFFFFF) << fileHeader.indexShift;
            uint64_t blockEndPosition = uint64_t(blocks[currentBlock + 1] & 0x7FFFFFFF) << fileHeader.indexShift;
            uint64_t currentBlockSize = blockEndPosition - blockStartPosition;

            // The current block size cannot exceed 2 x blockSize.
            if (currentBlockSize > (fileHeader.blockSize * 2))
            {
                // Looks like the header is corrupted
                fprintf(stderr, "\n\nERROR: The input file header is corrupt. Corrupted index block.\n\n");
                return_code = 1;
                goto exit;
            }

            // Read the block data
            inFile.read(readBuffer.data(), currentBlockSize);

            int decompressedBytes = decompress_block(
                readBuffer.data(),
                currentBlockSize,
                writeBuffer.data(),
                writeBuffer.size(),
                uncompressed);

            if (decompressedBytes > 0)
            {
                outFile.write(writeBuffer.data(), decompressedBytes);
            }
            else
            {
                fprintf(stderr, "ERROR: There was an error decompressing the source file.\n");
                return_code = 1;
                goto exit;
            }

            progress_decompress(inFile.tellg(), inputSize);
        }
    }

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

uint32_t compress_block(
    const char *src,
    uint32_t srcSize,
    char *dst,
    uint32_t dstSize,
    bool &uncompressed,
    opt settings)
{
    // Try to compress the data into the dst buffer
    uint32_t outSize = 0;
    if (settings.lz4hc)
    {
        LZ4_streamHC_t lz4_state;
        LZ4_resetStreamHC(&lz4_state, settings.compressionLevel);
        outSize = LZ4_compress_HC_continue(&lz4_state, src, dst, srcSize, dstSize);

        // outSize = LZ4_compress_HC(src, dst, srcSize, dstSize, settings.compressionLevel);
    }
    else
    {
        LZ4_stream_t lz4_state;
        LZ4_resetStream(&lz4_state);
        outSize = LZ4_compress_fast_continue(&lz4_state, src, dst, srcSize, dstSize, lz4_compression_level[settings.compressionLevel - 1]);

        // outSize = LZ4_compress_fast(src, dst, srcSize, dstSize, lz4_compression_level[settings.compressionLevel - 1]);
    }

    // If the block was not compressed because a buffer space problem, or the output is bigger than input
    //
    if (outSize == 0 || outSize > srcSize)
    {
        if (dstSize < srcSize)
        {
            // The block cannot be compressed and the raw data doesn't fit the dst buffer
            return 0;
        }
        uncompressed = true;
        std::memcpy(dst, src, srcSize);
        return srcSize;
    }
    else
    {
        uncompressed = false;
        return outSize;
    }
}

uint32_t decompress_block(
    const char *src,
    uint32_t srcSize,
    char *dst,
    uint32_t dstSize,
    bool uncompressed)
{
    if (uncompressed)
    {
        // If the data is non compressed, then just copy the source data
        if (dstSize > srcSize)
        {
            // The raw input data is less than the buffer
            return 0;
        }

        std::memcpy(dst, src, dstSize);
        return dstSize;
    }
    else
    {
        return LZ4_decompress_safe_partial(src, dst, srcSize, dstSize, dstSize);
    }
}

void file_align(std::fstream &fOut, uint8_t shift)
{
    uint16_t paddingLostBytes = fOut.tellp() % (1 << shift);
    if (paddingLostBytes)
    {
        uint16_t alignment = (1 << shift) - paddingLostBytes;
        for (uint64_t i = 0; i < alignment; i++)
        {
            fOut.write("\0", 1);
        }
    }
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

    while ((ch = getopt_long(argc, argv, "i:o:c:lb:fkh", long_options, NULL)) != -1)
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

                if (temp_argument < 1 || temp_argument > 12)
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

        // short option '-f', long option "--force"
        case 'l':
            options.lz4hc = true;
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
            "    -c/--compression 1-12\n"
            "           Compression level to be used. By default 12.\n"
            "    -l/--lz4hc\n"
            "           Uses the LZ4 high compression algorithm to improve the compression ratio.\n"
            "           NOTE: This will create a non standar ZSO and maybe the decompressor will not be compatible.\n"
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

static void progress_compress(uint64_t currentInput, uint64_t totalInput, uint64_t currentOutput)
{
    uint8_t progress = (currentInput * 100) / totalInput;
    uint8_t ratio = (currentOutput * 100) / currentInput;

    if (lastProgress != progress || lastRatio != ratio)
    {
        fprintf(stderr, "%050s\r", "");
        fprintf(stderr, "Compressing(%u%%) - Ratio(%u%%)\r", progress, ratio);
        lastProgress = progress;
        lastRatio = ratio;
    }
}

static void progress_decompress(uint64_t currentInput, uint64_t totalInput)
{
    uint8_t progress = (currentInput * 100) / totalInput;

    if (lastProgress != progress)
    {
        fprintf(stderr, "%050s\r", "");
        fprintf(stderr, "Decompressing(%u%%)\r", progress);
        lastProgress = progress;
    }
}