// Added to TF-PSA-Crypto's default configuration when Mbed TLS 4 is fetched (libs/ssl/CMakeLists.txt).
// Without the threading layer PSA crypto is not thread-safe, and Mbed TLS 4 does all of its
// cryptography through it: two sockets shaking hands on two threads at once failed now and then
// (handshake errors -0x6E00 and -0x3B00, which Haxe throws as Eof).
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
