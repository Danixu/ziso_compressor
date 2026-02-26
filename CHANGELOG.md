# Ziso Changelog

## v0.7.0
* Removed the blocksize detection because an incompatibility with OPL with blocks bigger than 2048 bytes. I recommend to keep the default configuration.
* Code quality improvements
* Updated the spdlog and lz4 compressor versions
* Added the MacOS binaries

## v0.6.1
* Added the spdlog library to print the output log.
* Fixed the Github action build for Windows
* Moved the Windows version to posix
* Fixed a bug when the last block doesn't matches the blocksize

## v0.5.2

* Fixed a bug compressing/decompressing non standard blocksize files, like for example CDROM's (blocksize 2352)
* Fixed a bug calculating the compression percentage and its ratio in progress message.

## v0.5.1

* Added a fix to a bug in the hdl_dump program which causes corruption when a compressed file is copied and its size is not divisible by 2048.

## v0.5.0

* First version of the program.
* Includes two LZ4 compression methods whith different results. Their result can vary depending of the source data. By default the same as ziso.py will be used.
* Brute force compression to select the best of the two methods for every block.
* LZ4HC compression is included.
* Faster than the original ziso.py compressor.
* Blocks with bigger compressed size are not compressed (like the original ziso.py tool).
* Ability to detect source CDROM to adjust the block size to their sector size (2352). This will improve the decompression speed by reducing the number of blocks to decompress to get a full sector (1 vs 2).