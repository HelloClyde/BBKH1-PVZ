#ifndef H1_PVZ_RESOURCES_H
#define H1_PVZ_RESOURCES_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Logical game dimensions remain independent of offline storage dimensions.
 * H1I1 sources have identical dimensions; H1I2 can store a reduced bitmap. */
typedef struct H1ImageInfo {
    uint32_t width, height, channels, flags, pixel_bytes;
    uint32_t storage_width, storage_height;
} H1ImageInfo;
typedef struct H1Image {
    void *file;
    H1ImageInfo info;
    uint32_t alpha_color;
    /* Reader-owned state. Only h1_image_* functions may operate on file. */
    uint32_t rgb_source_bytes, rgb_next_byte, rgb_stream_valid;
    uint32_t payload_offset;
} H1Image;
#define H1_IMAGE_MASK_ONLY 1u
#define H1_IMAGE_OPTIONAL_TRANSPARENT 2u
typedef void (*H1ResourceLogger)(const char *message);

void h1_resources_set_root(const char *root);
void h1_resources_set_logger(H1ResourceLogger logger);
const char *h1_resources_get_root(void);
int h1_image_open(H1Image *image, const char *logical_path, int auto_alpha, uint32_t alpha_color);
void h1_image_close(H1Image *image);
int h1_image_probe(const char *logical_path, int auto_alpha, H1ImageInfo *info);
int h1_image_read_span(H1Image *image, uint32_t x, uint32_t y, uint32_t count, uint32_t *argb);
int h1_image_load_all(H1Image *image, uint32_t *argb, size_t capacity_pixels);
/* Physical pixels for the renderer. No logical-size expansion occurs. */
int h1_image_read_storage_span(H1Image *image, uint32_t x, uint32_t y, uint32_t count, uint32_t *argb);
int h1_image_load_storage(H1Image *image, uint32_t *argb, size_t capacity_pixels);
/* Exact physical A8 bytes; keeps font masks in their offline representation. */
int h1_image_load_storage_alpha(H1Image *image,unsigned char *alpha,size_t capacity);
/* Exact stored RGB24 bytes from an opened H1I1 RGB3/flags0 source. Offsets
 * and lengths are payload-relative and must both be divisible by three.
 * Consecutive calls share one stdio stream; other image reads invalidate its
 * sequential position. A short read returns zero and invalidates that position.
 * No ARGB conversion or allocation occurs. Zero bytes at a valid boundary are
 * allowed, including EOF, and need no output buffer. */
int h1_image_read_rgb_bytes(H1Image *image, uint32_t byte_offset, uint32_t byte_count, unsigned char *rgb);
/* Pixel-unit wrapper for 1024-pixel / 3072-byte pages; may cross source rows. */
int h1_image_read_rgb_span(H1Image *image, uint32_t first_pixel, uint32_t pixel_count, unsigned char *rgb);

#ifdef __cplusplus
}
#endif
#endif
