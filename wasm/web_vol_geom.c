#include "vol_geom.h"
#include <emscripten.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* CPP */

static vol_geom_info_t _info;
static vol_geom_frame_data_t _frame_data;
static char _seq_filename[256];

EMSCRIPTEN_KEEPALIVE
bool create_file_info( const char* hdr_filename, const char* seq_filename ) {
  _seq_filename[0] = '\0';
  strncat( _seq_filename, seq_filename, 255 );

  emscripten_wget( hdr_filename, hdr_filename );
  emscripten_wget( seq_filename, seq_filename );

  printf( "hdr,seq = %s,%s\n", hdr_filename, seq_filename );

  return vol_geom_create_file_info( hdr_filename, seq_filename, &_info, true );
}

EMSCRIPTEN_KEEPALIVE
bool free_file_info() { return vol_geom_free_file_info( &_info ); }

EMSCRIPTEN_KEEPALIVE
bool read_frame( int frame_idx ) {
  // printf("reading frame %i\n", frame_idx );
  return vol_geom_read_frame( _seq_filename, &_info, frame_idx, &_frame_data );
}

EMSCRIPTEN_KEEPALIVE
bool is_keyframe( int frame_idx ) {
  bool is_key = vol_geom_is_keyframe( &_info, frame_idx );
  // printf("checking for key %i = %i\n", frame_idx, is_key);
  return is_key;
}

EMSCRIPTEN_KEEPALIVE
int find_previous_keyframe( int frame_idx ) { return vol_geom_find_previous_keyframe( &_info, frame_idx ); }

EMSCRIPTEN_KEEPALIVE
uint8_t* frame_vertices() { return &_frame_data.block_data_ptr[_frame_data.vertices_offset]; }

EMSCRIPTEN_KEEPALIVE
int32_t frame_vertices_sz() { return _frame_data.vertices_sz; }

EMSCRIPTEN_KEEPALIVE
int32_t frame_uvs_sz() { return _frame_data.uvs_sz; }

EMSCRIPTEN_KEEPALIVE
uint8_t* frame_i() { // 'frame_indices' name REFUSED to export
  return &_frame_data.block_data_ptr[_frame_data.indices_offset];
}

EMSCRIPTEN_KEEPALIVE
int32_t frame_i_sz() { return _frame_data.indices_sz; }

EMSCRIPTEN_KEEPALIVE
uint8_t* frame_data_ptr() { return _frame_data.block_data_ptr; }

EMSCRIPTEN_KEEPALIVE
uint32_t frame_vp_offset() { return _frame_data.vertices_offset; }

static float* vp_ptr;
static size_t prev_vp_ptr_sz;

static float* vt_ptr;
static size_t prev_vt_ptr_sz;

static uint16_t* indices_ptr;
static size_t prev_indices_ptr_sz;

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
uint16_t* frame_indices_copied( void ) {
  if ( _frame_data.indices_sz > prev_indices_ptr_sz ) {
    indices_ptr         = realloc( indices_ptr, _frame_data.indices_sz );
    prev_indices_ptr_sz = _frame_data.indices_sz;
  }
  if ( !indices_ptr ) { return indices_ptr; }
  uint16_t* u16_ptr = (uint16_t*)&_frame_data.block_data_ptr[_frame_data.indices_offset];
  memcpy( indices_ptr, u16_ptr, _frame_data.indices_sz );
  // printf("first 3 indices: %u, %u, %u\n", (uint32_t)u16_ptr[0], (uint32_t)u16_ptr[1], (uint32_t)u16_ptr[2] );
  return indices_ptr;
}

#ifdef __cplusplus
}
#endif /* CPP */
