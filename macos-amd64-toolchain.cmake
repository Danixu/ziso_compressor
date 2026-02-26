set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR aarch64) # Cambia a x86_64 si compilas para Mac Intel

set(CMAKE_C_COMPILER "zig;cc")
set(CMAKE_CXX_COMPILER "zig;c++")

# Target genérico para Mac
set(TARGET_ARCH "x86_64-macos")

# ESTA ES LA MAGIA: CMake forzará el "--target=" en todo el proceso
# sin importar qué variables sobrescriba tu CMakeLists.txt
set(CMAKE_C_COMPILER_TARGET ${TARGET_ARCH})
set(CMAKE_CXX_COMPILER_TARGET ${TARGET_ARCH})

# Asumimos que todo funciona para saltar las pruebas
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
set(CMAKE_CROSSCOMPILING_EMULATOR "")

set(APPLE TRUE)