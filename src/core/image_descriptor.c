// Allocate official C SDK descriptors without invoking its C++ link-time helpers.
#include <nvCVImage.h>
#include <stdlib.h>
NvCVImage* nvvfx_create_image_descriptor(void) { return calloc(1, sizeof(NvCVImage)); }
void nvvfx_free_image_descriptor(NvCVImage* image) { free(image); }
