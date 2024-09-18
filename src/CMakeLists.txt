add_executable(ziso ziso.cpp)
#target_compile_features(ziso PRIVATE cxx_std_11)
target_include_directories(ziso PUBLIC
    ../include/ 
    ../lib/lz4/lib/
    ../lib/spdlog/include/
)
set_target_properties(ziso PROPERTIES CXX_STANDARD 23)

target_link_libraries(ziso PRIVATE stdc++ -static lz4)