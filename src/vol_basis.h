/***
 * TODO
 * add a web wrapper with emscripten_keepalive macros and point casts to int32 etc
 */

#pragma once

#ifdef _WIN32
/** If building a library with Visual Studio, we need to explicitly 'export' symbols. This generates a .lib file to go with the .dll dynamic library file. */
#define VOL_BASIS_EXPORT __declspec( dllexport )
#else
/** If building a library with Visual Studio, we need to explicitly 'export' symbols. This generates a .lib file to go with the .dll dynamic library file. */
#define VOL_BASIS_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif /* CPP */

#include <stdbool.h>
#include <stdint.h>

VOL_BASIS_EXPORT bool vol_basis_init( void );

VOL_BASIS_EXPORT bool vol_basis_transcode( //
  int format,                              /// Matches transcoder_texture_format enum values from basisu_transcoder.h (cTFBC3_RGBA = 3)
  void* data_ptr,                          // Input: Basis-compressed data from sequence frame.
  uint32_t data_sz,                        // Input: Data size in bytes from sequence frame.
  uint8_t* output_blocks_ptr,              // Output: Transcoded compressed texture data to use.
  uint32_t output_blocks_sz,               //
  int* w_ptr, int* h_ptr                   // Output: Dimensions of texture.
);

VOL_BASIS_EXPORT bool vol_basis_free( void );

#ifdef __cplusplus
}
#endif /* CPP */
