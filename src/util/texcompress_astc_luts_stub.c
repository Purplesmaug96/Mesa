/* Stub for the ASTC decoder LUTs ...
 *
 * The real LUTs live in C++ files (texcompress_astc_luts*.cpp) which cannot
 * be compiled for the console, but st_texcompress_compute.c still takes
 * their address at link time.  The compute-based ASTC path is never
 * reachable on the console (it needs a compute-capable GL 4.x context), so
 * empty stubs are safe.
 */

#include <stdint.h>
#include <stddef.h>

void _mesa_init_astc_decoder_luts(void *holder)
{
  (void)holder;
}

void *_mesa_get_astc_decoder_partition_table(uint32_t block_width,
                                             uint32_t block_height)
{
  (void)block_width;
  (void)block_height;
  return NULL;
}

/* ABI-compatible stand-in for _mesa_unpack_astc_2d_ldr, which the state
 * tracker references through texcompress_astc.h.  The console backend
 * never unpacks ASTC textures on the CPU (no mipmap CPU decompression of
 * ASTC is reachable on the fixed pipeline), so this can stay empty; it is
 * only a link-time filler.
 */
void _mesa_unpack_astc_2d_ldr(uint8_t *rgba, const uint8_t *blkData,
                              uint32_t block_width, uint32_t block_height)
{
  (void)rgba;
  (void)blkData;
  (void)block_width;
  (void)block_height;
}