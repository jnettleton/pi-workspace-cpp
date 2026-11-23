// stb_image_write, used by the picture tests to make JPEG and PNG files.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "stb_image_write.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
