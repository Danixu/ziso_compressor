add_executable(ziso ziso.cpp)
#target_compile_features(ziso PRIVATE cxx_std_11)
target_include_directories(ziso PUBLIC ../include/)
set_target_properties(ziso PROPERTIES CXX_STANDARD 17)

target_link_libraries(ziso PRIVATE stdc++ -static lz4)