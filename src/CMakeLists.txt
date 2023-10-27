add_executable(ziso ziso.cpp)
target_compile_features(ziso PRIVATE cxx_std_17)
target_include_directories(ziso PUBLIC ../include/)

target_link_libraries(ziso PRIVATE lz4)