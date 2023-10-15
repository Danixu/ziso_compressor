#include "ziso.h"
#include "../lib/lz4/lib/lz4.h"
#include "../lib/lz4/lib/lz4hc.h"

static struct option long_options[] = {
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"compression", required_argument, NULL, 'c'},
    {"mode2-lz4", no_argument, NULL, 'm'},
    {"lz4hc", no_argument, NULL, 'h'},
    {"brute-force", no_argument, NULL, 'b'},
    {"block-size", required_argument, NULL, 's'},
    {"force", no_argument, NULL, 'f'},
    {"keep-output", no_argument, NULL, 'k'},
    {NULL, 0, NULL, 0}};

// global variales
uint8_t lastProgress = 100; // Force at 0% of progress
summary summaryData;

int main(int argc, char **argv)
{
    // Start the timer to measure execution time
    auto start = std::chrono::high_resolution_clock::now();

    // Return code.
    int return_code = 0;

    // Main options
    opt options;

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

    return_code = get_options(argc, argv, options);
    if (return_code)
    {
        goto exit;
    }

    if (options.inputFile.empty())
    {
        fprintf(stderr, "ERROR: input file is required.\n");
        print_help();
        return_code = 1;
        goto exit;
    }

    // Open the input file
    inFile.open(options.inputFile.c_str(), std::ios::in | std::ios::binary);
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
            options.compress = false;
        }
        else
        {
            fprintf(stdout, "ISO file detected. Compressing to ZISO\n");
        }
    }

    // If no output filename was provided, generate it using the input filename
    if (options.outputFile.empty())
    {
        // Remove the extensión
        std::string rawName = options.inputFile.substr(0, options.inputFile.find_last_of("."));

        // Input file will be decoded, so ecm2 extension must be removed (if exists)
        if (options.compress)
        {
            options.outputFile = rawName + ".zso";
        }
        else
        {
            options.outputFile = rawName + ".iso";
        }
    }

    // Check if output file exists only if force_rewrite is false
    if (options.overwrite == false)
    {
        char dummy;
        outFile.open(options.outputFile.c_str(), std::ios::in | std::ios::binary);
        if (outFile.read(&dummy, 0))
        {
            fprintf(stderr, "ERROR: Cowardly refusing to replace output file. Use the -f/--force-rewrite options to force it.\n");
            options.keepOutput = true;
            return_code = 1;
            goto exit;
        }
        outFile.close();
    }

    // Open the output file in replace mode
    outFile.open(options.outputFile.c_str(), std::ios::out | std::ios::binary);
    // Check if file was oppened correctly.
    if (!outFile.good())
    {
        fprintf(stderr, "ERROR: output file cannot be opened.\n");
        return_code = 1;
        goto exit;
    }

    if (options.compress)
    {
        // Get the input size
        inFile.seekg(0, std::ios_base::end);
        inputSize = inFile.tellg();
        inFile.seekg(0, std::ios_base::beg);

        // Get the total blocks
        blocksNumber = ceil((float)inputSize / options.blockSize) + 1;
        // Calculate the header size
        headerSize = 0x18 + (blocksNumber * sizeof(uint32_t));

        // Set the header input size and block size
        fileHeader.uncompressedSize = inputSize;
        fileHeader.blockSize = options.blockSize;

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

        if (options.bruteForce && options.lz4hc)
        {
            fprintf(stderr, "WARNING: The brute-force method will try the best between the two Standard LZ4 methods. LZ4HC already uses the best method, so no brute-force is required. LZ4HC flag will be ignored...\n");
        }

        // Print the sumary
        fprintf(stdout, "%20s %s\n", "Source:", options.inputFile.c_str());
        fprintf(stdout, "%20s %s\n\n", "Destination:", options.outputFile.c_str());
        fprintf(stdout, "%20s %lu bytes\n", "Total File Size:", inputSize);
        fprintf(stdout, "%20s %d\n", "Block Size:", options.blockSize);
        fprintf(stdout, "%20s %d\n", "Index align:", fileHeader.indexShift);
        fprintf(stdout, "%20s %d\n", "Compress Level:", options.compressionLevel);
        if (options.bruteForce)
        {
            fprintf(stdout, "%20s Yes\n", "Brute Force Search:");
        }
        else
        {
            fprintf(stdout, "%20s No\n", "Brute Force Search:");
        }
        if (options.lz4hc)
        {
            fprintf(stdout, "%20s Yes\n", "LZ4 HC Compression:");
        }
        else
        {
            fprintf(stdout, "%20s %d\n", "LZ4 acceleration:", lz4_compression_level[options.compressionLevel - 1]);
            if (options.alternativeLz4)
            {
                fprintf(stdout, "%20s Yes\n", "LZ4 Mode 2:");
            }
            else
            {
                fprintf(stdout, "%20s No\n", "LZ4 Mode 2:");
            }
            fprintf(stdout, "%20s No\n", "LZ4 HC Compression:");
        }

        outFile.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));

        // Reserve the blocks index space
        blocks.resize(blocksNumber, 0);

        outFile.write((const char *)blocks.data(), blocksNumber * sizeof(uint32_t));

        readBuffer.resize(options.blockSize, 0);
        writeBuffer.resize(options.blockSize, 0);

        for (uint32_t currentBlock = 0; currentBlock < blocksNumber - 1; currentBlock++)
        {
            // Fill the output with zeroes until a valid start point depending of index shift
            file_align(outFile, fileHeader.indexShift);

            // Capture the block position
            uint64_t blockStartPosition = outFile.tellp();

            uint64_t toRead = options.blockSize;
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
                options);

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

        outFile.seekp(0, std::ios_base::end);
        show_summary(outFile.tellp(), options);
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
        fprintf(stdout, "%20s %s\n", "Source:", options.inputFile.c_str());
        fprintf(stdout, "%20s %s\n\n", "Destination:", options.outputFile.c_str());
        fprintf(stdout, "%20s %lu bytes\n", "Total File Size:", fileHeader.uncompressedSize);
        fprintf(stdout, "%20s %d\n", "Block Size:", fileHeader.blockSize);
        fprintf(stdout, "%20s %d\n", "Index align:", fileHeader.indexShift);

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
        if (!options.keepOutput)
        {
            // Something went wrong, so output file must be deleted if keep == false
            // We will remove the file if something went wrong
            fprintf(stderr, "\n\nERROR: there was an error processing the input file.\n\n");
            std::ifstream out_remove_tmp(options.outputFile.c_str(), std::ios::binary);
            char dummy;
            if (out_remove_tmp.read(&dummy, 0))
            {
                out_remove_tmp.close();
                if (remove(options.outputFile.c_str()))
                {
                    fprintf(stderr, "There was an error removing the output file... Please remove it manually.\n");
                }
            }
        }
    }
    return return_code;
}

inline uint32_t compress_block(
    const char *src,
    uint32_t srcSize,
    char *dst,
    uint32_t dstSize,
    bool &uncompressed,
    opt options)
{
    // The source size will be the same always
    summaryData.sourceSize += srcSize;

    // Try to compress the data into the dst buffer
    uint32_t outSize = 0;
    if (options.bruteForce)
    {
        // This method will try all the available compression methods to select the most apropiate.
        uint32_t lz4Size = 0;
        uint32_t lz4Method2Size = 0;

        std::vector<char> lz4Buffer(dstSize, 0);
        std::vector<char> lz4Method2Buffer(dstSize, 0);

        bool counted = false;

        // Compress using the standard methods
        // Method 1
        LZ4_stream_t lz4_state;
        LZ4_resetStream(&lz4_state);
        lz4Size = LZ4_compress_fast_continue(&lz4_state, src, lz4Buffer.data(), srcSize, dstSize, lz4_compression_level[options.compressionLevel - 1]);
        // Method 2
        lz4Method2Size = LZ4_compress_fast(src, lz4Method2Buffer.data(), srcSize, dstSize, lz4_compression_level[options.compressionLevel - 1]);

        // Get the smaller output between all the methods
        if (lz4Size > 0 && (lz4Size < outSize || outSize == 0))
        {
            outSize = lz4Size;
        }
        if (lz4Method2Size > 0 && (lz4Method2Size < outSize || outSize == 0))
        {
            outSize = lz4Method2Size;
        }

        // If there was an error compressing or the size is bigger than source, don't do anything.
        if (outSize == 0 || outSize >= srcSize)
        {
            // The raw data will be copied later
        }
        // The methods priority are LZ4, LZ4 Method 2.
        else if (lz4Size > 0 && outSize == lz4Size)
        {
            std::memcpy(dst, lz4Buffer.data(), lz4Size);

            summaryData.lz4Count++;
            summaryData.lz4In += srcSize;
            summaryData.lz4Out += outSize;
        }
        else if (lz4Method2Size > 0 && outSize == lz4Method2Size)
        {
            std::memcpy(dst, lz4Method2Buffer.data(), lz4Method2Size);

            summaryData.lz4m2Count++;
            summaryData.lz4m2In += srcSize;
            summaryData.lz4m2Out += outSize;
        }
        else
        {
            // Something weird
            return 0;
        }
    }
    else
    {
        if (options.lz4hc)
        {

            // outSize = LZ4_compress_HC(src, dst, srcSize, dstSize, options.compressionLevel);

            LZ4_streamHC_t lz4_state;
            LZ4_resetStreamHC(&lz4_state, options.compressionLevel);
            outSize = LZ4_compress_HC_continue(&lz4_state, src, dst, srcSize, dstSize);
        }
        else
        {
            if (options.alternativeLz4)
            {
                outSize = LZ4_compress_fast(src, dst, srcSize, dstSize, lz4_compression_level[options.compressionLevel - 1]);
            }
            else
            {
                LZ4_stream_t lz4_state;
                LZ4_resetStream(&lz4_state);
                outSize = LZ4_compress_fast_continue(&lz4_state, src, dst, srcSize, dstSize, lz4_compression_level[options.compressionLevel - 1]);
            }
        }
    }

    // If the block was not compressed because a buffer space problem, or the output is bigger than input
    //
    if (outSize == 0 || outSize >= srcSize)
    {
        if (dstSize < srcSize)
        {
            // The block cannot be compressed and the raw data doesn't fit the dst buffer
            return 0;
        }
        uncompressed = true;
        std::memcpy(dst, src, srcSize);

        summaryData.rawCount++;
        summaryData.raw += srcSize;

        return srcSize;
    }
    else
    {
        uncompressed = false;

        // When brute force is used, summary data is added before.
        if (!options.bruteForce)
        {
            if (options.lz4hc)
            {
                summaryData.lz4hcCount++;
                summaryData.lz4hcIn += srcSize;
                summaryData.lz4hcOut += outSize;
            }
            else
            {
                if (options.alternativeLz4)
                {
                    summaryData.lz4m2Count++;
                    summaryData.lz4m2In += srcSize;
                    summaryData.lz4m2Out += outSize;
                }
                else
                {
                    summaryData.lz4Count++;
                    summaryData.lz4In += srcSize;
                    summaryData.lz4Out += outSize;
                }
            }
        }

        return outSize;
    }
}

inline uint32_t decompress_block(
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

    while ((ch = getopt_long(argc, argv, "i:o:c:mhbs:fk", long_options, NULL)) != -1)
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

        // short option '-m', long option "--method2-lz4"
        case 'm':
            options.alternativeLz4 = true;
            break;

        // short option '-h', long option "--lz4hc"
        case 'h':
            options.lz4hc = true;
            break;

        // short option '-f', long option "--force"
        case 'b':
            options.bruteForce = true;
            break;

        // short option '-b', long option "--block-size"
        case 's':
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

        case '?':
            print_help();
            return 1;
            break;
        }
    }

    return 0;
}

void print_help()
{
    banner();
    fprintf(stdout,
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
            "    -m/--mode2-lz4\n"
            "           Uses an alternative compression method which will reduce the size in some cases.\n"
            "    -h/--lz4hc\n"
            "           Uses the LZ4 high compression algorithm to improve the compression ratio.\n"
            "           NOTE: This will create a non standar ZSO and maybe the decompressor will not be compatible.\n"
            "    -b/--brute-force\n"
            "           SLOW: Try to compress using the two LZ4 methods. LZ4HC already selects the best compression method.\n"
            "    -s/--block-size <size>\n"
            "           The size in bytes of the blocks. By default 2048.\n"
            "    -f/--force\n"
            "           Force to ovewrite the output file\n"
            "    -k/--keep-output\n"
            "           Keep the output when something went wrong, otherwise will be removed on error.\n"
            "\n");
}

static void progress_compress(uint64_t currentInput, uint64_t totalInput, uint64_t currentOutput)
{
    uint8_t progress = (currentInput * 100) / totalInput;
    uint8_t ratio = (currentOutput * 100) / currentInput;

    if (lastProgress != progress)
    {
        fprintf(stdout, "%050s\r", "");
        fprintf(stdout, "Compressing(%u%%) - Ratio(%u%%)\r", progress, ratio);
        fflush(stdout);
        lastProgress = progress;
    }
}

static void progress_decompress(uint64_t currentInput, uint64_t totalInput)
{
    uint8_t progress = (currentInput * 100) / totalInput;

    if (lastProgress != progress)
    {
        fprintf(stdout, "%050s\r", "");
        fprintf(stdout, "Decompressing(%u%%)\r", progress);
        fflush(stdout);
        lastProgress = progress;
    }
}

static void show_summary(uint64_t outputSize, opt options)
{
    uint32_t total_sectors = summaryData.lz4Count + summaryData.lz4m2Count + summaryData.lz4hcCount + summaryData.rawCount;
    fprintf(stdout, "\n\n");
    fprintf(stdout, " ZSO compression sumpary\n");
    fprintf(stdout, "--------------------------------------------------------------\n");
    fprintf(stdout, " Type                Sectors        In Size          Out Size \n");
    fprintf(stdout, "--------------------------------------------------------------\n");
    if (options.bruteForce || (!options.lz4hc && !options.alternativeLz4))
    {
        fprintf(stdout, "LZ4 ............... %7d ...... %7.2fMB ...... %7.2fMB\n", summaryData.lz4Count, MB(summaryData.lz4In), MB(summaryData.lz4Out));
    }
    if (options.bruteForce || (!options.lz4hc && options.alternativeLz4))
    {
        fprintf(stdout, "LZ4 M2 ............ %7d ...... %7.2fMB ...... %7.2fMB\n", summaryData.lz4m2Count, MB(summaryData.lz4m2In), MB(summaryData.lz4m2Out));
    }
    if (!options.bruteForce && options.lz4hc)
    {
        fprintf(stdout, "LZ4HC ............. %7d ...... %7.2fMB ...... %7.2fMB\n", summaryData.lz4hcCount, MB(summaryData.lz4hcIn), MB(summaryData.lz4hcOut));
    }
    fprintf(stdout, "RAW ............... %7d ...... %7.2fMB ...... %7.2fMB\n", summaryData.rawCount, MB(summaryData.raw), MB(summaryData.raw));
    fprintf(stdout, "--------------------------------------------------------------\n");
    fprintf(stdout, "Total ............. %7d ...... %7.2fMb ...... %7.2fMb\n", total_sectors, MB(summaryData.sourceSize), MB(outputSize));
    fprintf(stdout, "ZSO reduction (input vs ZSO) ...................... %3.2f%%\n", (1.0 - (outputSize / (float)summaryData.sourceSize)) * 100);
    fprintf(stdout, "\n\n");
}