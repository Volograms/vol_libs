/**  @file wasm_vol_geom.c
 *           | Web-assembly wrapper for Volograms Geometry Decoding API
 * --------- | ---------------------------------------------
 * Authors   | Anton Gerdelan     <anton@volograms.com>
 *           | Patrick Geoghegan  <patrick@volograms.com>
 * Copyright | 2022-2023, Volograms (http://volograms.com/)
 * Language  | C99
 * Files     | 1
 * Licence   | The MIT License. See LICENSE.md for details.
 * Version   | 1.1 Added texture functions to support Basis Universal transcoding.
 *           | 1.0 First version.
 */

#include "vol_geom.h"
#include "vol_basis.h"
#include <emscripten.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif /* CPP */

static vol_geom_info_t _info;
static vol_geom_frame_data_t _frame_data;
static char _seq_filename[256];

EMSCRIPTEN_KEEPALIVE
unsigned int do_usleep( unsigned int us ) { return usleep( us ); }

EMSCRIPTEN_KEEPALIVE
bool has_normals( void ) { return _info.hdr.normals; }

EMSCRIPTEN_KEEPALIVE
bool has_texture( void ) { return _info.hdr.textured; }

EMSCRIPTEN_KEEPALIVE
bool has_audio( void ) { return _info.hdr.audio; }

EMSCRIPTEN_KEEPALIVE
int32_t texture_width( void ) { return (int32_t)_info.hdr.texture_width; }

EMSCRIPTEN_KEEPALIVE
int32_t texture_height( void ) { return (int32_t)_info.hdr.texture_height; }

EMSCRIPTEN_KEEPALIVE 
int32_t texture_compression( void ) { return (int32_t)_info.hdr.texture_compression; }

EMSCRIPTEN_KEEPALIVE 
int32_t texture_container_format( void ) { return (int32_t)_info.hdr.texture_container_format; }

EMSCRIPTEN_KEEPALIVE
bool create_file_info( const char* hdr_filename, const char* seq_filename ) {
  _seq_filename[0] = '\0';
  strncat( _seq_filename, seq_filename, 255 );
  return vol_geom_create_file_info( hdr_filename, seq_filename, &_info, true );
}

EMSCRIPTEN_KEEPALIVE
bool create_single_file_info( const char* vol_filename ) {
  _seq_filename[0] = '\0';
  strncat( _seq_filename, vol_filename, 255 );
  return vol_geom_create_file_info_from_file( vol_filename, &_info );
}


EMSCRIPTEN_KEEPALIVE
int32_t frame_count( void ) { return (int32_t)_info.hdr.frame_count; }

EMSCRIPTEN_KEEPALIVE
bool read_frame( int frame_idx ) {
  return vol_geom_read_frame( _seq_filename, &_info, frame_idx, &_frame_data );
}

EMSCRIPTEN_KEEPALIVE
bool update_frames_directory( int frame_idx ) {
  // Check if we're in streaming mode
  if ( _info.streaming_buffer_ptr && _info.streaming_buffer_ptr->is_streaming_mode ) {
    // In streaming mode: check if frame is available in streaming buffers
    // First try to update the buffer frame directory to parse any new frames
    vol_geom_update_buffer_frame_directory( &_info );
    
    // Then check if the specific frame is available
    return vol_geom_is_frame_available_in_buffer( &_info, (uint32_t)frame_idx );
  } else {
    // In file mode: use original file-based directory update
    return vol_geom_update_frames_directory( _seq_filename, &_info, frame_idx );
  }
}


EMSCRIPTEN_KEEPALIVE
int32_t max_blob_sz( void ) {
  return _info.biggest_frame_blob_sz;
}

EMSCRIPTEN_KEEPALIVE
bool is_keyframe( int frame_idx ) {
  bool is_key = vol_geom_is_keyframe( &_info, frame_idx );
  return is_key;
}

EMSCRIPTEN_KEEPALIVE
int find_previous_keyframe( int frame_idx ) { return vol_geom_find_previous_keyframe( &_info, frame_idx ); }

EMSCRIPTEN_KEEPALIVE
uint8_t* frame_vertices( void ) { return &_frame_data.block_data_ptr[_frame_data.vertices_offset]; }

EMSCRIPTEN_KEEPALIVE
int32_t frame_vertices_sz( void ) { return _frame_data.vertices_sz; }

EMSCRIPTEN_KEEPALIVE
int32_t frame_uvs_sz( void ) { return _frame_data.uvs_sz; }

EMSCRIPTEN_KEEPALIVE
int32_t frame_normals_sz( void ) { return _frame_data.normals_sz; }

EMSCRIPTEN_KEEPALIVE
uint8_t* frame_texture_data_ptr( void ) { return &_frame_data.block_data_ptr[_frame_data.texture_offset]; }

EMSCRIPTEN_KEEPALIVE
int32_t frame_texture_sz( void ) { return _frame_data.texture_sz; }

EMSCRIPTEN_KEEPALIVE
uint8_t* frame_i( void ) { // 'frame_indices' name REFUSED to export
  return &_frame_data.block_data_ptr[_frame_data.indices_offset];
}

EMSCRIPTEN_KEEPALIVE
int32_t frame_i_sz( void ) { return _frame_data.indices_sz; }

EMSCRIPTEN_KEEPALIVE
uint8_t* frame_data_ptr( void ) { return _frame_data.block_data_ptr; }

EMSCRIPTEN_KEEPALIVE
uint32_t frame_vp_offset( void ) { return _frame_data.vertices_offset; }

static float* vp_ptr;
static size_t prev_vp_ptr_sz = 0;

static float* vt_ptr;
static size_t prev_vt_ptr_sz = 0;

static float* vn_ptr;
static size_t prev_vn_ptr_sz = 0;

static uint16_t* indices_ptr;
static size_t prev_indices_ptr_sz = 0;

EMSCRIPTEN_KEEPALIVE
bool free_file_info( void ) { 
  if(prev_vp_ptr_sz > 0) {
    prev_vp_ptr_sz = 0;
    free(vp_ptr);
  }
  if(prev_vt_ptr_sz > 0) {
    prev_vt_ptr_sz = 0;
    free(vt_ptr);
  }
  if(prev_vn_ptr_sz > 0) {
    prev_vn_ptr_sz = 0;
    free(vn_ptr);
  }
  if(prev_indices_ptr_sz > 0) {
    prev_indices_ptr_sz = 0;
    free(indices_ptr);
  }
  return vol_geom_free_file_info( &_info ); 
}



EMSCRIPTEN_KEEPALIVE
float* frame_vp_copied( void ) {
  if ( _frame_data.vertices_sz > prev_vp_ptr_sz ) {
    vp_ptr         = realloc( vp_ptr, _frame_data.vertices_sz );
    prev_vp_ptr_sz = _frame_data.vertices_sz;
  }
  if ( !vp_ptr ) { return vp_ptr; }
  float* f32_ptr = (float*)&_frame_data.block_data_ptr[_frame_data.vertices_offset];
  memcpy( vp_ptr, f32_ptr, _frame_data.vertices_sz );
  return vp_ptr;
}

EMSCRIPTEN_KEEPALIVE
float* frame_uvs_copied( void ) {
  if ( _frame_data.uvs_sz > prev_vt_ptr_sz ) {
    vt_ptr         = realloc( vt_ptr, _frame_data.uvs_sz );
    prev_vt_ptr_sz = _frame_data.uvs_sz;
  }
  if ( !vt_ptr ) { return vt_ptr; }
  float* f32_ptr = (float*)&_frame_data.block_data_ptr[_frame_data.uvs_offset];
  memcpy( vt_ptr, f32_ptr, _frame_data.uvs_sz );
  return vt_ptr;
}

EMSCRIPTEN_KEEPALIVE
float* frame_normals_copied( void ) {
  if ( _frame_data.normals_sz > prev_vn_ptr_sz ) {
    vn_ptr         = realloc( vn_ptr, _frame_data.normals_sz );
    prev_vn_ptr_sz = _frame_data.normals_sz;
  }
  if ( !vn_ptr ) { return vn_ptr; }
  float* f32_ptr = (float*)&_frame_data.block_data_ptr[_frame_data.normals_offset];
  memcpy( vn_ptr, f32_ptr, _frame_data.normals_sz );
  return vn_ptr;
}

EMSCRIPTEN_KEEPALIVE
uint16_t* frame_indices_copied( void ) {
  if ( _frame_data.indices_sz > prev_indices_ptr_sz ) {
    indices_ptr         = realloc( indices_ptr, _frame_data.indices_sz );
    prev_indices_ptr_sz = _frame_data.indices_sz;
  }
  if ( !indices_ptr ) { return indices_ptr; }
  uint16_t* u16_ptr = (uint16_t*)&_frame_data.block_data_ptr[_frame_data.indices_offset];
  memcpy( indices_ptr, u16_ptr, _frame_data.indices_sz );
  return indices_ptr;
}

static const int _dims_presize                       = 2048;
static uint32_t _blocks_buf_size_in_blocks_or_pixels = _dims_presize * _dims_presize;
static uint8_t* _output_blocks_ptr;

EMSCRIPTEN_KEEPALIVE
bool basis_init( void ) {
  _output_blocks_ptr = malloc( _dims_presize * _dims_presize );
  if ( !_output_blocks_ptr ) {
    fprintf( stderr, "ERROR: basis_init malloc failed\n" );
    return false;
  }
  bool res = vol_basis_init();
  if ( !res ) {
    fprintf( stderr, "ERROR: basis_init - vol_basis_init failed\n" );
    return false;
  }
  return true;
}

static int _transcoded_w = 0;
static int _transcoded_h = 0;
static uint32_t _transcoded_sz = 0;

EMSCRIPTEN_KEEPALIVE
bool basis_transcode( int format, void* data_ptr, uint32_t data_sz ) {
  _transcoded_w = 0;
  _transcoded_h = 0;
  _transcoded_sz = 0;
  bool ret = vol_basis_transcode_v2( format, data_ptr, data_sz, _output_blocks_ptr, _blocks_buf_size_in_blocks_or_pixels, &_transcoded_w, &_transcoded_h, &_transcoded_sz );
  return ret;
}

EMSCRIPTEN_KEEPALIVE
int basis_get_transcoded_width( void ) { return _transcoded_w; }

EMSCRIPTEN_KEEPALIVE
int basis_get_transcoded_height( void ) { return _transcoded_h; }

EMSCRIPTEN_KEEPALIVE
uint32_t basis_get_transcoded_sz_v2( void ) { return _transcoded_sz; }

EMSCRIPTEN_KEEPALIVE
uint8_t* basis_get_transcoded_ptr( void ) { return _output_blocks_ptr; }

EMSCRIPTEN_KEEPALIVE
uint32_t basis_get_transcoded_sz( void ) { return _blocks_buf_size_in_blocks_or_pixels; }

EMSCRIPTEN_KEEPALIVE
bool run_basis_transcode( int format ) {
  _transcoded_w = 0;
  _transcoded_h = 0;
  _transcoded_sz = 0;
  uint8_t* data_ptr = &_frame_data.block_data_ptr[_frame_data.texture_offset];
  int32_t data_sz = _frame_data.texture_sz;
  bool ret = vol_basis_transcode_v2( format, data_ptr, data_sz, _output_blocks_ptr, _blocks_buf_size_in_blocks_or_pixels, &_transcoded_w, &_transcoded_h, &_transcoded_sz );
  return ret;
}

EMSCRIPTEN_KEEPALIVE
bool basis_free( void ) {
  if ( !_output_blocks_ptr ) { return false; }
  free( _output_blocks_ptr );
  _output_blocks_ptr = NULL;
  return true;
}

EMSCRIPTEN_KEEPALIVE
uint8_t* audio_data_ptr( void ) { return _info.audio_data_ptr; }

EMSCRIPTEN_KEEPALIVE
uint32_t audio_data_sz( void ) { return _info.audio_data_sz; }

//
// ===== STREAMING BUFFER WASM EXPORTS =====
// WASM wrapper functions for the new streaming buffer API
//

// Static streaming configuration for WASM use
static vol_geom_streaming_config_t _streaming_config;

EMSCRIPTEN_KEEPALIVE
bool init_streaming_config( void ) {
  return vol_geom_init_streaming_config( &_streaming_config );
}

EMSCRIPTEN_KEEPALIVE
int should_use_streaming_mode( int file_size ) {
  printf("File size: %d\n", file_size);
  printf("Max buffer size: %lld\n", (long long)_streaming_config.max_buffer_size);
  printf("Lookahead seconds: %f\n", _streaming_config.lookahead_seconds);
  printf("Auto select mode: %d\n", _streaming_config.auto_select_mode);
  printf("Force streaming mode: %d\n", _streaming_config.force_streaming_mode);
  printf("Should use streaming mode: %d\n", vol_geom_should_use_streaming_mode( (vol_geom_size_t)file_size, &_streaming_config ));
  return vol_geom_should_use_streaming_mode( (vol_geom_size_t)file_size, &_streaming_config ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int create_streaming_buffer( void ) {
  return vol_geom_create_streaming_buffer( &_info, &_streaming_config ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int add_data_to_buffer( uint8_t* data_ptr, int data_size ) {
  if ( !data_ptr || data_size <= 0 ) {
    return 0;
  }
  return vol_geom_add_data_to_buffer( &_info, data_ptr, (vol_geom_size_t)data_size ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int update_buffer_frame_directory( void ) {
  return vol_geom_update_buffer_frame_directory( &_info ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int read_frame_streaming( int frame_idx ) {
  return vol_geom_read_frame_streaming( &_info, frame_idx, &_frame_data ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int is_frame_available_in_buffer( int frame_idx ) {
  return vol_geom_is_frame_available_in_buffer( &_info, frame_idx ) ? 1 : 0;
}

// EMSCRIPTEN_KEEPALIVE
// int get_buffer_health_bytes( void ) {
//   return (int)vol_geom_get_buffer_health_bytes( &_info );
// }

EMSCRIPTEN_KEEPALIVE
float get_buffer_health_seconds( float fps ) {
  return vol_geom_get_buffer_health_seconds( &_info, fps );
}

EMSCRIPTEN_KEEPALIVE
int should_resume_download( int current_frame, float fps ) {
  return vol_geom_should_resume_download( &_info, current_frame, fps ) ? 1 : 0;
}

// Configuration getters/setters for JavaScript access
EMSCRIPTEN_KEEPALIVE
int get_max_buffer_size( void ) {
  return (int)_streaming_config.max_buffer_size;
}

EMSCRIPTEN_KEEPALIVE
void set_max_buffer_size( int size ) {
  _streaming_config.max_buffer_size = (vol_geom_size_t)size;
}

EMSCRIPTEN_KEEPALIVE
float get_lookahead_seconds( void ) {
  return _streaming_config.lookahead_seconds;
}

EMSCRIPTEN_KEEPALIVE
void set_lookahead_seconds( float seconds ) {
  _streaming_config.lookahead_seconds = seconds;
}

EMSCRIPTEN_KEEPALIVE
int get_auto_select_mode( void ) {
  return _streaming_config.auto_select_mode ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void set_auto_select_mode( int enabled ) {
  _streaming_config.auto_select_mode = enabled ? true : false;
}

EMSCRIPTEN_KEEPALIVE
int get_force_streaming_mode( void ) {
  return _streaming_config.force_streaming_mode ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void set_force_streaming_mode( int enabled ) {
  _streaming_config.force_streaming_mode = enabled ? true : false;
}

// ===== DUAL BUFFER MANAGEMENT EXPORTS =====
// Additional functions for the dual buffer streaming system

EMSCRIPTEN_KEEPALIVE
int is_download_buffer_full( void ) {
  return vol_geom_is_download_buffer_full( &_info ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int update_buffer_state( void ) {
  return vol_geom_update_buffer_state( &_info ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int get_playback_buffer_size( void ) {
  vol_geom_size_t buffer_size = 0;
  const uint8_t* buffer = vol_geom_get_playback_buffer( &_info, &buffer_size );
  return buffer ? (int)buffer_size : 0;
}

EMSCRIPTEN_KEEPALIVE
int create_streaming_file_info( void ) {
  return vol_geom_create_streaming_file_info( &_info ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int get_header_frame_body_start( void ) {
  return vol_geom_get_sequence_offset( &_info );
}

EMSCRIPTEN_KEEPALIVE
void reset_frame_directory( void ) {
  vol_geom_reset_frame_directory( &_info );
}


#ifdef __cplusplus
}
#endif /* CPP */
