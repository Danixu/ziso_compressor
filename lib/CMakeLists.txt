# Make an automatic library - will be static or dynamic based on user setting
add_library(lz4 lz4/lib/lz4.c)

# We need this directory, and users of our library will need it too
target_include_directories(lz4 PUBLIC lz4/lib/)

# All users of this library will need at least C++11
target_compile_features(lz4 PUBLIC cxx_std_11)