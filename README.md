# ZISO compressor

This ziso compressor is an alternative to the ziso.py compressor that I have found in the OPL repository. It doesn't respect the compression level and doesn't provide LZ4HC compression, so I have decided to create my own compressor.

## Features

* Allows to set the compression level by using the LZ4 feature called "acceleration".
* Includes the LZ4HC compression algorithm.
* It includes an alternative compression method which can reduce the size in some cases.
* Brute force compression to use the best compression method between the two LZ4 methods.

## ToDo

There are some things I'd like to do, like for example:

* Try to add read and write buffers to improve the speed
* Add Multi Thread processing.
* Maybe add an extra step to analyze the ISO. The space saving in the best scenario is about 4,5MB, so will be the lower priority.

## Compile

Clone the repository:

```
git clone git@github.com:Danixu/ziso_compressor.git
```

Download the submodules:

```
cd ziso_compressor
git submodules update --init
```

Compile the library:

```
cd src
g++ -I../include ../lib/lz4/lib/lz4.c ../lib/lz4/lib/lz4hc.c ziso.cpp -O3 -o <ziso or ziso.exe>
```

Tested in Manjaro and Windows 11 with MSYS2 with MinGW64.

## Usage

The program is easy to use, and the output filename will be determined if not provided. Also it is able to detect the ZISO files, so will determine if the file must be compressed or uncompressed.

Compress/decompress a file:

```
ziso.exe -i <input-file>

ziso.exe -i <input-file> -o <output-file>
```

## Arguments

| Short |      Long     | Value |                      Description                      |
|:-----:|:-------------:|:-----:|:-----------------------------------------------------:|
|   -i  | --input       |       | Input file to process                                 |
|   -o  | --output      |       | Output file                                           |
|   -c  | --compression |  1-12 | Compression level                                     |
|   -m  | --mode2-lz4   |       | Use an alternative LZ4 compression method             |
|   -h  | --lz4hc       |       | Activate the High Compression algorithm               |
|   -b  | --brute-force |       | Test the two LZ4 compression methods and use the best |
|   -s  | --block-size  |  2048 | Block size, usually 2048 but better 2352 for CD-ROMS  |
|   -f  | --force       |       | Force to overwrite the output file                    |
|   -k  | --keep-output |       | Keep the output file if something fails               |


### Explanation

#### Compression

The LZ4 method doesn't have compression level arguments. Instead, it has **acceleration** which will affects the speed and compression ratio (just like the compression arguments). On my program I use the **acceleration** to supply the compression level.

The LZ4HC method already includes the compression option, so it will passed to it directly.

#### Alternative LZ4 compression method

The LZ4 library has two ways to compress the data. One is intended to be used to compress the data at once, and the other to compress several blocks of data keeping the dictionary to improve the compression ratio.

In this case both are used in the same way: compress all the data at once, but for any reason there are some differences between both even when they uses the same algorithm. Sometimes the first method compress better and sometimes the 2nd. That is why I provide the "alternative" method to be used when it compress a bit better.

The default compression method produces the same output as the ziso.py program.

#### LZ4 High Compression

The LZ4 library provides an alternative compression method called LZ4HC, which provides a better compression ratio in most scenarios. It is also slower and some programs and hardwares can be incompatible with this compression method. That is why I only recommend to use if when we are fully sure that the reader is compatible. For example, the ziso.py compressor is able to decompress this files even when it doesn't have the compression method. Also moderns emulators can be compatible with it.

#### Brute Force Search

As I have explained above, there are two compression functions in LZ4. This argument will compress every block using both and will write the smaller result to the output file.

LZ4HC will not be tried because it also uses the best compression method, so using the brute-force option with the LZ4HC compression method included will produce the same result as to use directly the lz4hc compression option. That is why is better to directly use that option unless we want to keep the compatibility with the standard LZ4 libraries without HC.