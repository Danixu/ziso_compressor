#include "ziso.h"
#include "lz4.h"
#include "lz4hc.h"
#include "spdlog/sinks/basic_file_sink.h"

// Arguments list
const char *const short_options = "i:o:c:b:rh";
const std::vector<option> long_options = {
    // Long and short options
    {"input", required_argument, nullptr, 'i'},
    {"output", required_argument, nullptr, 'o'},
    {"compression-level", required_argument, nullptr, 'c'},
    {"block-size", required_argument, nullptr, 'b'},
    {"replace", no_argument, nullptr, 'r'},
    {"help", no_argument, nullptr, 'h'},

    // Long only options
    {"mode2-lz4", no_argument, nullptr, 10},
    {"lz4hc", no_argument, nullptr, 11},
    {"brute-force", no_argument, nullptr, 12},
    {"cache-size", required_argument, nullptr, 13},
    {"hdl-fix", no_argument, nullptr, 14},
    {"log-file", required_argument, nullptr, 15},
    {"log-level", required_argument, nullptr, 16},
    {"ignore-header-size", no_argument, nullptr, 17},
    {nullptr, 0, nullptr, 0}};

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

    // Progress
    uint8_t lastProgress = 100; // Force at 0% of progress
    summary summaryData;

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

    spdlog::debug("Checking the input file.");

    if (options.inputFile.empty())
    {
        spdlog::error("Input file is required.");
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
            spdlog::error("Input file cannot be opened.");
            return_code = 1;
            goto exit;
        }
    }

    // Check if the file is a ZISO File
    {
        std::vector<char> file_format(5, 0);
        inFile.read(file_format.data(), 4);

        if (
            file_format[0] == 'Z' &&
            file_format[1] == 'I' &&
            file_format[2] == 'S' &&
            file_format[3] == 'O')
        {
            // File is a ZISO file, so will be decompressed
            spdlog::info("ZISO file detected. Decompressing...");
            options.compress = false;
        }
        else
        {
            spdlog::info("ISO file detected. Compressing to ZISO...");
        }
    }

    // If no output filename was provided, generate it using the input filename
    if (options.outputFile.empty())
    {
        spdlog::debug("Ouput file not provided, so will be generated using the input filename.");
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
        spdlog::debug("The output filename is: {}", options.outputFile);
    }

    if (options.inputFile == options.outputFile)
    {
        spdlog::error("The input and output is the same file. Check the arguments and the input file extension.");
        return_code = 1;
        goto exit;
    }

    // Check if output file exists only if force_rewrite is false
    if (options.overwrite == false)
    {
        char dummy;
        outFile.open(options.outputFile.c_str(), std::ios::in | std::ios::binary);
        if (outFile.read(&dummy, 0))
        {
            spdlog::error("Cowardly refusing to replace the output file. Use the -r/--replace options to force it.");
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
        spdlog::error("Output file cannot be opened.");
        return_code = 1;
        goto exit;
    }

    // Print the selected options in debug mode:
    spdlog::debug("Option inputFile: {}", options.inputFile);
    spdlog::debug("Option outputFile: {}", options.outputFile);
    spdlog::debug("Option compress: {}", options.compress);
    spdlog::debug("Option cacheSize: {}", options.cacheSize);
    spdlog::debug("Option overwrite: {}", options.overwrite);
    spdlog::debug("Option logFile: {}", options.logFile);
    spdlog::debug("Option logLevel: {}", std::to_underlying(options.logLevel));
    spdlog::debug("Option keepOutput: {}", options.keepOutput);

    if (options.compress)
    {
        spdlog::info("Compressing the input file.");
        // Get the input size
        inFile.seekg(0, std::ios_base::end);
        inputSize = inFile.tellg();
        inFile.seekg(0, std::ios_base::beg);
        spdlog::debug("The input file size is {} bytes.", inputSize);

        if (!options.blockSizeFixed)
        {
            if (is_cdrom(inFile))
            {
                spdlog::warn("CD-ROM detected... It's recommended to convert the file to ISO.");
            }
        }
        else if (options.blockSize != 2048)
        {
            spdlog::warn(
                "OPL is not compatible with blocks size bigger than 2048. If you plan to use this ZSO on OPL, "
                "please check if your OPL version is compatible.");
        }

        // Get the total blocks
        blocksNumber = ceil((float)inputSize / options.blockSize) + 1;
        spdlog::debug("Number of blocks in file: {}.", blocksNumber - 1);
        spdlog::debug("Last block size: {}. (0 means 'BlockSize')", inputSize % options.blockSize);
        // Calculate the header size
        headerSize = 0x18 + (blocksNumber * sizeof(uint32_t));

        spdlog::debug("Option blockSizeFixed: {}", options.blockSizeFixed);
        spdlog::debug("Option blockSize: {}", options.blockSize);
        spdlog::debug("Option compressionLevel: {}", options.compressionLevel);
        spdlog::debug("Option alternativeLz4: {}", options.alternativeLz4);
        spdlog::debug("Option bruteForce: {}", options.bruteForce);
        spdlog::debug("Option lz4hc: {}", options.lz4hc);
        spdlog::debug("Option hdlFix: {}", options.hdlFix);

        // Set the header input size and block size
        fileHeader.uncompressedSize = inputSize;
        fileHeader.blockSize = options.blockSize;

        // Set shift depending of the input size. Bigger shift means more waste.
        if (inputSize > (uint64_t)(0x3FFFFFFFF - headerSize))
        {
            // Size is bigger than 17.179.869.183 (16GB-32GB). PS2 games aren't that big.
            fileHeader.indexShift = 4;
        }
        if (inputSize > (uint64_t)(0x1FFFFFFFF - headerSize))
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
            spdlog::warn("The brute-force method will try the best between the two Standard LZ4 methods. LZ4HC already uses the best method, so no brute-force is required. LZ4HC flag will be ignored...");
        }

        // Print the sumary
        spdlog::info("{:<20s} {}", "Source:", options.inputFile.c_str());
        spdlog::info("{:<20s} {}", "Destination:", options.outputFile.c_str());
        spdlog::info("{:<20s} {} bytes", "Total File Size:", inputSize);
        spdlog::info("{:<20s} {}", "Block Size:", options.blockSize);
        spdlog::info("{:<20s} {}", "Index align:", fileHeader.indexShift);
        spdlog::info("{:<20s} {}", "Compress Level:", options.compressionLevel);
        if (options.bruteForce)
        {
            spdlog::info("{:20s} Yes", "Brute Force Search:");
        }
        else
        {
            spdlog::info("{:<20s} No", "Brute Force Search:");
        }
        if (options.lz4hc)
        {
            spdlog::info("{:<20s} Yes", "LZ4 HC Compression:");
        }
        else
        {
            spdlog::info("{:<20s} {}", "LZ4 acceleration:", lz4_compression_level[options.compressionLevel - 1]);
            if (options.alternativeLz4)
            {
                spdlog::info("{:<20s} Yes", "LZ4 Mode 2:");
            }
            else
            {
                spdlog::info("{:<20s} No", "LZ4 Mode 2:");
            }
            spdlog::info("{:<20s} No", "LZ4 HC Compression:");
        }

        spdlog::debug("Writing the file header.");
        outFile.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));

        // Reserve the blocks index space
        spdlog::debug("Reserving the blocks index.");
        blocks.resize(blocksNumber, 0);

        spdlog::debug("Writing the blocks index into the output file.");
        outFile.write((const char *)blocks.data(), blocksNumber * sizeof(uint32_t));

        // Read buffer. To make it easier to manage, we will create a buffer with a size of a multiple of the blockSize.
        uint32_t readBufferSize = options.cacheSize - (options.cacheSize % options.blockSize);
        spdlog::debug("The read buffer size will be {}.", readBufferSize);
        if (inputSize < readBufferSize)
        {
            readBufferSize = inputSize - (inputSize % options.blockSize);
            spdlog::debug("The input file is smaller than the buffer, so buffer will be adjusted to {} bytes.", readBufferSize);
        }
        uint32_t readBufferPos = readBufferSize; // Set the position to the End Of Buffer to force a fill at the first loop.
        spdlog::debug("Reserving the read buffer space.");
        std::vector<char> readBuffer(readBufferSize, 0);
        // Write buffer. The output block size is not fixed, so cannot be calculated and we will use the cache size.
        uint32_t writeBufferPos = 0;
        spdlog::debug("Reserving the write buffer space.");
        std::vector<char> writeBuffer(options.cacheSize, 0);

        for (uint32_t currentBlock = 0; currentBlock < blocksNumber - 1; currentBlock++)
        {
            spdlog::trace("Compressing the block {}.", currentBlock + 1);

            // If the current reader position is the end of the buffer, fill the buffer with new data
            if (readBufferPos >= (readBufferSize - 1))
            {
                spdlog::trace("The read buffer is empty. Filling it with the input file data.");
                uint32_t inputReadBytes = readBufferSize;
                uint64_t leftInFile = inputSize - (uint64_t)inFile.tellg();
                if (inputReadBytes > leftInFile)
                {
                    inputReadBytes = leftInFile;
                }
                spdlog::trace("{} bytes will be read from input file", inputReadBytes);
                inFile.read(readBuffer.data(), inputReadBytes);
                readBufferPos = 0;
            }

            // Fill the output with zeroes until a valid start point depending of index shift
            spdlog::trace("Aligning the output buffer to the nearest shifted position.");
            uint16_t alignment = buffer_align(writeBuffer.data() + writeBufferPos, (uint64_t)outFile.tellp() + writeBufferPos, fileHeader.indexShift);
            writeBufferPos += alignment;
            spdlog::trace("The new aligned position is {}.", (uint64_t)outFile.tellp() + writeBufferPos);

            // Capture the block position
            uint64_t blockStartPosition = (uint64_t)outFile.tellp() + writeBufferPos;

            uint64_t toRead = options.blockSize;
            uint64_t leftInFile = inputSize - ((uint64_t)inFile.tellg() - ((readBufferSize - 1) - readBufferPos));
            spdlog::trace(
                "Input Size: {} - Input Position: {} - Read Buffer Size: {} - Read Buffer Position: {} - LeftInFile: {}",
                inputSize,
                (uint64_t)inFile.tellg(),
                readBufferSize,
                readBufferPos,
                leftInFile);
            if (leftInFile < toRead)
            {
                toRead = leftInFile;
            }
            spdlog::trace("To Read: {}", toRead);

            bool uncompressed = false;
            int compressedBytes = compress_block(
                readBuffer.data() + readBufferPos,
                toRead,
                writeBuffer.data() + writeBufferPos,
                options.blockSize,
                uncompressed,
                options,
                summaryData);
            spdlog::trace("CompressedBytes: {}", compressedBytes);

            readBufferPos += toRead;

            if (compressedBytes > 0)
            {
                writeBufferPos += compressedBytes;

                spdlog::trace(
                    "Output Position: {} - Output Buffer Size: {} - Output Buffer Position: {} - Block Compressed Size: {}",
                    (uint64_t)outFile.tellg(),
                    writeBuffer.size(),
                    writeBufferPos,
                    compressedBytes);

                if (
                    ((writeBuffer.size() - writeBufferPos) < (options.blockSize * 2)) ||
                    (currentBlock == (blocksNumber - 2)))
                {
                    spdlog::trace("Flushing write buffer...");
                    outFile.write(writeBuffer.data(), writeBufferPos);
                    writeBufferPos = 0;
                }
            }
            else
            {
                spdlog::error("There was an error compressing the source file.");
                return_code = 1;
                goto exit;
            }

            // Set the current block start point with the uncompressed flag
            blocks[currentBlock] = (blockStartPosition >> fileHeader.indexShift) | ((uint32_t)uncompressed << 31);

            // Update the progress
            progress_compress((uint64_t)inFile.tellg() + readBufferPos, inputSize, blockStartPosition - headerSize, lastProgress);
        }

        spdlog::trace("Aligning the last block from: {}...", (uint64_t)outFile.tellp());
        // Align the file and set the eof position block
        file_align(outFile, fileHeader.indexShift);
        uint64_t blockEndPosition = outFile.tellp();
        blocks[blocksNumber - 1] = (blockEndPosition >> fileHeader.indexShift);
        spdlog::trace("Aligned block position: {}...", (uint64_t)outFile.tellp());

        // The HDL_dump bug trims the data at the end of the file if doesn't fit into a 2048 multiple.
        // This fix will pad the output file to the nearest 2048 bytes multiple.
        if (options.hdlFix)
        {
            spdlog::trace("Aplying the HDL fix to avoid the files to be truncated on copy");
            file_align(outFile, 11);
        }

        // Write the blocks index
        spdlog::trace("Writting the index data (overwrite)");
        outFile.seekp(0x18);
        outFile.write((const char *)blocks.data(), blocksNumber * sizeof(uint32_t));
        spdlog::trace("Writen {} bytes at {} position", blocksNumber * sizeof(uint32_t), 0x18);

        outFile.seekp(0, std::ios_base::end);
        show_summary(outFile.tellp(), options, summaryData);
    }
    else
    {
        spdlog::info("Decompressing the input file.");
        // Get the input size
        inFile.seekg(0, std::ios_base::end);
        inputSize = inFile.tellg();
        inFile.seekg(0, std::ios_base::beg);
        spdlog::debug("The input file size is {} bytes.", inputSize);

        // Read the header
        inFile.read(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));

        // Calculate the blocks number
        blocksNumber = ceil((float)fileHeader.uncompressedSize / fileHeader.blockSize) + 1;

        // Reserve and read the blocks index
        blocks.resize(blocksNumber, 0);
        inFile.read((char *)blocks.data(), blocksNumber * sizeof(uint32_t));

        // Set the options blocksize from the fileheader blocksize
        options.blockSize = fileHeader.blockSize;

        // Print the sumary
        spdlog::info("{:<20s} {}", "Source:", options.inputFile.c_str());
        spdlog::info("{:<20s} {}", "Destination:", options.outputFile.c_str());
        spdlog::info("{:<20s} {} bytes", "Total File Size:", fileHeader.uncompressedSize);
        spdlog::info("{:<20s} {}", "Block Size:", fileHeader.blockSize);
        spdlog::info("{:<20s} {}", "Index align:", fileHeader.indexShift);
        spdlog::debug("Number of blocks in file: {}.", blocksNumber - 1);

        // Check if the input file is damaged
        uint64_t headerFileSize = uint64_t(blocks[blocksNumber - 1] & 0x7FFFFFFF) << fileHeader.indexShift;
        // Check if the file was fixed against the hdl_dump bug
        uint64_t hdlFixHeaderFileSize = headerFileSize;
        if (headerFileSize % 2048)
        {
            hdlFixHeaderFileSize = ((headerFileSize >> 11) + 1) << 11;
        }

        if (headerFileSize != inputSize && hdlFixHeaderFileSize != inputSize && options.ignoreHeaderSize == false)
        {
            // The input file doesn't matches the index data and maybe is damaged
            spdlog::error("The input file header is corrupt. Filesize doesn't matches.");
            spdlog::debug("Input file size: {} - Header file size: {} - hdlFixed size: {}.", inputSize, headerFileSize, hdlFixHeaderFileSize);
            return_code = 1;
            goto exit;
        }

        // Read buffer. The input block size is not fixed, so cannot be calculated and we will use the cache size.
        uint32_t readBufferSize = options.cacheSize;
        if (inputSize < readBufferSize)
        {
            readBufferSize = inputSize;
        }
        uint32_t readBufferPos = readBufferSize; // Set the position to the End Of Buffer to force a fill at the first loop.
        std::vector<char> readBuffer(readBufferSize, 0);
        // Write buffer. To make it easier to manage, we will create a buffer with a size of a multiple of the blockSize.
        uint32_t writeBufferSize = options.cacheSize - (options.cacheSize % options.blockSize);
        uint32_t writeBufferPos = 0;
        std::vector<char> writeBuffer(writeBufferSize, 0);

        for (uint32_t currentBlock = 0; currentBlock < blocksNumber - 1; currentBlock++)
        {
            bool uncompressed = blocks[currentBlock] & 0x80000000;
            uint64_t blockStartPosition = uint64_t(blocks[currentBlock] & 0x7FFFFFFF) << fileHeader.indexShift;
            uint64_t blockEndPosition = uint64_t(blocks[currentBlock + 1] & 0x7FFFFFFF) << fileHeader.indexShift;
            uint64_t currentBlockSize = blockEndPosition - blockStartPosition;

            spdlog::trace(
                "Current Block: {} - Block Start Position: {} - Block End Position: {} - Block Size: {} - Uncompressed: {}",
                currentBlock + 1,
                blockStartPosition,
                blockEndPosition,
                currentBlockSize,
                uncompressed);

            // The current block size cannot exceed 2 x blockSize.
            if (currentBlockSize > (fileHeader.blockSize * 2))
            {
                // Looks like the header is corrupted
                spdlog::error("The input file header is corrupt. Corrupted index block.");
                return_code = 1;
                goto exit;
            }

            //  If the current reader position is the end of the buffer, fill the buffer with new data
            if (uint32_t leftInReadBuffer = readBufferSize - readBufferPos;
                currentBlockSize > leftInReadBuffer)
            {
                // At the first block, the input file must be synced to the start point or can be missaligned
                if (currentBlock == 0)
                {
                    inFile.seekg(blockStartPosition);
                }
                spdlog::trace("The reader buffer is empty... reading more data.");
                // Get the data left in file
                uint64_t leftInFile = inputSize - inFile.tellg();
                spdlog::trace("There are {} bytes left in the file.", leftInFile);

                // To Read
                uint64_t toRead = readBufferSize - leftInReadBuffer;
                if (toRead > leftInFile)
                {
                    toRead = leftInFile;
                }
                spdlog::trace("{} bytes will be read.", toRead);

                // Move the data buffer to the start point
                std::memmove(readBuffer.data(), readBuffer.data() + readBufferPos, leftInReadBuffer);
                spdlog::trace("Current file position: {}", (uint64_t)inFile.tellp());
                inFile.read(readBuffer.data() + leftInReadBuffer, toRead);
                spdlog::trace("New file position: {}", (uint64_t)inFile.tellp());
                readBufferPos = 0;
            }

            int decompressedBytes = decompress_block(
                readBuffer.data() + readBufferPos,
                currentBlockSize,
                writeBuffer.data() + writeBufferPos,
                options.blockSize,
                uncompressed);

            if (currentBlock == (blocksNumber - 2))
            {
                // The LZ4 compressor seems not to be taking into account of the input data size to limit the decompressed data
                // Or at least is not reporting the correct decompressed size.
                // This is a fix to avoid this problem calculating the correct last block size instead to use the LZ4 compressor reported size.
                decompressedBytes = fileHeader.uncompressedSize - ((uint64_t)outFile.tellp() + writeBufferPos);
                spdlog::trace("Fixed the last block size to: {}", decompressedBytes);
            }

            spdlog::trace("Decompressed data bytes: {}", decompressedBytes);

            if (decompressedBytes > 0)
            {
                readBufferPos += currentBlockSize;
                writeBufferPos += decompressedBytes;

                if (
                    writeBufferPos >= (writeBufferSize - 1) ||
                    (currentBlock == (blocksNumber - 2)))
                {
                    outFile.write(writeBuffer.data(), writeBufferPos);
                    writeBufferPos = 0;
                }
            }
            else
            {
                spdlog::error("There was an error decompressing the source file.");
                return_code = 1;
                goto exit;
            }

            progress_decompress(inFile.tellg(), inputSize, lastProgress);
        }

        if ((uint64_t)outFile.tellp() != fileHeader.uncompressedSize)
        {
            spdlog::error("The output filesize doesn't matches the header filesize. {} vs {}", fileHeader.uncompressedSize, (uint64_t)outFile.tellp());
            return_code = 1;
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
        spdlog::info("The file was processed without any problem");
        spdlog::info("Total execution time: {:0.3f}s", executionTime.count() / 1000.0F);
    }
    else
    {
        if (!options.keepOutput)
        {
            // Something went wrong, so output file must be deleted if keep == false
            // We will remove the file if something went wrong
            spdlog::error("there was an error processing the input file.");
            std::ifstream out_remove_tmp(options.outputFile.c_str(), std::ios::binary);
            char dummy;
            if (out_remove_tmp.read(&dummy, 0))
            {
                out_remove_tmp.close();
                if (remove(options.outputFile.c_str()))
                {
                    spdlog::error("There was an error removing the output file... Please remove it manually.");
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
    const opt &options,
    summary &summaryData)
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

        std::vector<char> lz4Buffer(LZ4_compressBound(dstSize), 0);
        std::vector<char> lz4Method2Buffer(LZ4_compressBound(dstSize), 0);

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

bool is_cdrom(std::fstream &fIn)
{
    // Store the current position
    uint64_t currentPos = fIn.tellg();

    // Seek to the start point
    fIn.seekg(0);

    // Read three sectors to ensure that the disk is a CDROM
    std::vector<char> buffer(12, 0);
    std::vector<char> cdSync = {(char)0x00, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, (char)0xFF, (char)0x00};
    for (uint8_t i = 0; i < 3; i++)
    {
        fIn.seekg(i * 2352);
        fIn.read(buffer.data(), buffer.size());

        //  Check if they matches
        if (buffer == cdSync)
        {
            fIn.seekg(currentPos);
            return true;
        }
    }
    fIn.seekg(currentPos);
    return false;
}

void file_align(std::fstream &fOut, uint8_t shift)
{
    if (uint16_t paddingLostBytes = fOut.tellp() % (1 << shift);
        paddingLostBytes)
    {
        uint16_t alignment = (1 << shift) - paddingLostBytes;
        for (uint64_t i = 0; i < alignment; i++)
        {
            fOut.write("\0", 1);
        }
    }
}

uint16_t buffer_align(char *buffer, uint64_t currentPosition, uint8_t shift)
{
    if (uint16_t paddingLostBytes = currentPosition % (1 << shift);
        paddingLostBytes)
    {
        uint16_t alignment = (1 << shift) - paddingLostBytes;
        std::memset(buffer, 0, alignment);
        return alignment;
    }
    return 0;
}

int get_options(
    int argc,
    char **argv,
    opt &options)
{
    char ch;
    // temporal variables for options parsing
    uint64_t temp_argument = 0;

    // Logger
    std::shared_ptr<spdlog::logger> logger = nullptr;

    std::string optarg_s;

    while ((ch = getopt_long(argc, argv, short_options, long_options.data(), nullptr)) != -1)
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

        // short option '-c', long option "--compression-level"
        // Compression level
        case 'c':
            try
            {
                optarg_s = optarg;
                temp_argument = std::stoi(optarg_s);

                if (temp_argument < 1 || temp_argument > 12)
                {
                    std::print(std::cerr, "\n\nERROR: the provided compression level option is not correct.\n\n");
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
                std::print(std::cerr, "\n\nERROR: the provided compression level is not correct.\n\n");
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
                    std::print(std::cerr, "\n\nERROR: the provided block size is not correct. Must be at least 512.\n\n");
                    print_help();
                    return 1;
                }
                else
                {
                    options.blockSize = (uint32_t)temp_argument;
                    options.blockSizeFixed = true;
                }
            }
            catch (std::exception const &e)
            {
                std::print(std::cerr, "\n\nERROR: the provided block size is not correct.\n\n");
                print_help();
                return 1;
            }
            break;

        // short option '-r', long option "--replace"
        case 'r':
            options.overwrite = true;
            break;

        // long option "--mode2-lz4"
        case 10:
            options.alternativeLz4 = true;
            break;

        // long option "--lz4hc"
        case 11:
            options.lz4hc = true;
            break;

        // long option "--brute-force"
        case 12:
            options.bruteForce = true;
            break;

        // long option "--cache-size"
        case 13:
            try
            {
                optarg_s = optarg;
                temp_argument = std::stoi(optarg_s);

                if (!temp_argument)
                {
                    std::print(std::cerr, "\n\nERROR: the provided cache size is not correct.\n\n");
                    print_help();
                    return 1;
                }
                else if (temp_argument > CACHE_SIZE_MAX)
                {
                    std::print(std::cerr, "\n\nERROR: the provided cache size is not correct. Must be less than {}MB\n\n", CACHE_SIZE_MAX);
                    print_help();
                    return 1;
                }
                else if (temp_argument < 1)
                {
                    std::print(std::cerr, "\n\nERROR: the provided cache size is not correct. Must be at least 1MB\n\n");
                    print_help();
                    return 1;
                }
                else
                {
                    options.cacheSize = (uint32_t)temp_argument * (1024 * 1024);
                }
            }
            catch (std::exception const &e)
            {
                std::print(std::cerr, "\n\nERROR: the provided block size is not correct.\n\n");
                print_help();
                return 1;
            }
            break;

        // long option "--hdl-fix"
        case 14:
            options.hdlFix = true;
            break;

        // long option '--log-file'
        case 15:
            options.logFile = optarg;
            logger = spdlog::basic_logger_mt("ziso", options.logFile);
            // logger->set_level(spdlog::level::trace);
            spdlog::set_default_logger(logger);
            break;

        // long option '--log-level'
        case 16:
            if (strcasecmp(optarg, "trace") == 0)
            {
                options.logLevel = spdlog::level::trace;
            }
            else if (strcasecmp(optarg, "debug") == 0)
            {
                options.logLevel = spdlog::level::debug;
            }
            else if (strcasecmp(optarg, "info") == 0)
            {
                options.logLevel = spdlog::level::info;
            }
            else if (strcasecmp(optarg, "warn") == 0)
            {
                options.logLevel = spdlog::level::warn;
            }
            else if (strcasecmp(optarg, "err") == 0)
            {
                options.logLevel = spdlog::level::err;
            }
            else if (strcasecmp(optarg, "critical") == 0)
            {
                options.logLevel = spdlog::level::critical;
            }
            else if (strcasecmp(optarg, "off") == 0)
            {
                options.logLevel = spdlog::level::off;
            }
            else
            {
                std::print(std::cerr, "\n\nERROR: The provided log level is incorrect.\n\n");
                print_help();
                return 1;
            }

            spdlog::set_level(options.logLevel);
            break;

        // Long option --ignore-header-size
        case 17:
            options.ignoreHeaderSize = true;
            break;

        default:
            print_help();
            return 1;
        }
    }

    return 0;
}

void print_help()
{
    banner();
    std::print(std::cout,
               "\n\nUsage:\n"
               "\n"
               "The program detects ziso sources and selects the decompression mode:\n"
               "    ziso -i/--input example.iso\n"
               "    ziso -i/--input example.iso -o/--output example.zso\n"
               "    ziso -i/--input example.zso\n"
               "    ziso -i/--input example.zso -o/--output example.iso\n"
               "Optional options:\n"
               "    -c/--compression-level 1-12\n"
               "           Compression level to be used. By default 12.\n"
               "    -b/--block-size <size>\n"
               "           The size in bytes of the blocks. By default 2048.\n"
               "    -r/--replace\n"
               "           Force to ovewrite the output file\n"
               "    --mode2-lz4\n"
               "           Uses an alternative compression method which will reduce the size in some cases.\n"
               "    --lz4hc\n"
               "           Uses the LZ4 high compression algorithm to improve the compression ratio.\n"
               "           NOTE: This will create a non standar ZSO and maybe the decompressor will not be compatible.\n"
               "    --brute-force\n"
               "           SLOW: Try to compress using the two LZ4 methods. LZ4HC already selects the best compression method.\n"
               "    --cache-size <size>\n"
               "           The size of the cache buffer in MB. By default {}. Memory usage will be the double ({}MB Read + {}MB Write).\n"
               "    --hdl-fix\n"
               "           Add a padding in the output file to the nearest upper 2048 bytes multiple (hdl_dump bug fix).\n"
               "    --log-file\n"
               "           Set the output log to a file.\n"
               "    --log-level\n"
               "           Set the log level between the following levels: trace, debug, info, warn, err, critical, off\n"
               "    --ignore-header-size\n"
               "           Ignore the output size stored in the header. Usefull to try to decompress the file even when file size is corrupted.\n"
               "\n",
               CACHE_SIZE_DEFAULT, CACHE_SIZE_DEFAULT, CACHE_SIZE_DEFAULT);
}

static void progress_compress(uint64_t currentInput, uint64_t totalInput, uint64_t currentOutput, uint8_t &lastProgress)
{
    uint8_t progress = (currentInput * 100) / totalInput;
    uint8_t ratio = (currentOutput * 100) / currentInput;

    if (lastProgress != progress)
    {
        std::print(std::cout, "{:50s}\r", "");
        std::print(std::cout, "Compressing({}%) - Ratio({}%)\r", progress, ratio);
        std::flush(std::cout);
        lastProgress = progress;
    }
}

static void progress_decompress(uint64_t currentInput, uint64_t totalInput, uint8_t &lastProgress)
{
    uint8_t progress = (currentInput * 100) / totalInput;

    if (lastProgress != progress)
    {
        std::print(std::cout, "{:50s}\r", "");
        std::print(std::cout, "Decompressing({}%)\r", progress);
        std::flush(std::cout);
        lastProgress = progress;
    }
}

static void show_summary(uint64_t outputSize, const opt &options, const summary &summaryData)
{
    uint32_t total_sectors = summaryData.lz4Count + summaryData.lz4m2Count + summaryData.lz4hcCount + summaryData.rawCount;
    std::print(std::cout, "\n\n");
    std::print(std::cout, " ZSO compression sumpary\n");
    std::print(std::cout, "---------------------------------------------------------------\n");
    std::print(std::cout, " Type                Sectors         In Size          Out Size \n");
    std::print(std::cout, "---------------------------------------------------------------\n");
    if (options.bruteForce || (!options.lz4hc && !options.alternativeLz4))
    {
        std::print(std::cout, " LZ4 ............... {:7d} ...... {:7.2f}MB ...... {:7.2f}MB\n", (unsigned long long)summaryData.lz4Count, MB(summaryData.lz4In), MB(summaryData.lz4Out));
    }
    if (options.bruteForce || (!options.lz4hc && options.alternativeLz4))
    {
        std::print(std::cout, " LZ4 M2 ............ {:7d} ...... {:7.2f}MB ...... {:7.2f}MB\n", (unsigned long long)summaryData.lz4m2Count, MB(summaryData.lz4m2In), MB(summaryData.lz4m2Out));
    }
    if (!options.bruteForce && options.lz4hc)
    {
        std::print(std::cout, " LZ4HC ............. {:7d} ...... {:7.2f}MB ...... {:7.2f}MB\n", (unsigned long long)summaryData.lz4hcCount, MB(summaryData.lz4hcIn), MB(summaryData.lz4hcOut));
    }
    std::print(std::cout, " RAW ............... {:7d} ...... {:7.2f}MB ...... {:7.2f}MB\n", (unsigned long long)summaryData.rawCount, MB(summaryData.raw), MB(summaryData.raw));
    std::print(std::cout, "---------------------------------------------------------------\n");
    std::print(std::cout, " Total ............. {:7d} ...... {:7.2f}MB ...... {:7.2f}MB\n", (unsigned long)total_sectors, MB(summaryData.sourceSize), MB(outputSize));
    std::print(std::cout, " ZSO reduction (input vs ZSO) ...................... {:8.2f}%\n", (1.0 - (outputSize / (float)summaryData.sourceSize)) * 100);
    std::print(std::cout, "\n\n");
}