/** @file geom_test.c
 * Unit tests for vol_geom using the cube vologram.
 * Not tested in this program: the buffering API.
 *           |
 * --------- | ----------
 * Authors   | Anton Gerdelan <anton@volograms.com>
 * Copyright | 2021, Volograms (http://volograms.com/)
 * Language  | C99
 * Licence   | The MIT License. See LICENSE.md for details.
 */

#include "vol_geom.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vol_geom_info_t geom_info;
static vol_geom_frame_data_t geom_frame_data;

static void _my_logger( vol_geom_log_type_t log_type, const char* message_str ) {
  FILE* stream_ptr = ( VOL_GEOM_LOG_TYPE_ERROR == log_type || VOL_GEOM_LOG_TYPE_WARNING == log_type ) ? stderr : stdout;
  fprintf( stream_ptr, "[vol_geom] %s", message_str ); // Prepend a tag to the logs so we can tell that it's working.
}

// simple custom assert() to exit with an error code and the file:line that called it whenever a unit test fails
#define VOL_ASSERT( x ) _vol_assert( x, __FILE__, __LINE__ );
static void _vol_assert( bool condition, const char* filename, int line ) {
  if ( !condition ) {
    fprintf( stderr, "TEST FAILED: %s:%i\n", filename, line );
    vol_geom_free_file_info( &geom_info );
    fprintf( stderr, "Unit tests failed\n" );
    exit( 1 );
  }
}

/// Convert from one of the Unity-format strings to a C-string
static void _short_str_to_c_str( char* c_str_ptr, const vol_geom_short_str_t* short_ptr ) {
  memcpy( c_str_ptr, short_ptr->bytes, short_ptr->sz );
  c_str_ptr[short_ptr->sz] = '\0';
}

typedef struct file_record_t {
  uint8_t* byte_ptr;
  size_t sz;
} file_record_t;

static bool _read_entire_file( const char* filename, file_record_t* fr_ptr ) {
  if ( !filename || !fr_ptr ) { return false; }
  FILE* f_ptr = fopen( filename, "rb" );
  if ( !f_ptr ) { return false; }
  fseek( f_ptr, 0L, SEEK_END );
  fr_ptr->sz       = (size_t)ftell( f_ptr );
  fr_ptr->byte_ptr = malloc( fr_ptr->sz );
  if ( !fr_ptr->byte_ptr ) {
    fclose( f_ptr );
    return false;
  }
  rewind( f_ptr );
  size_t nr = fread( fr_ptr->byte_ptr, fr_ptr->sz, 1, f_ptr );
  fclose( f_ptr );
  if ( 1 != nr ) { return false; }
  return true;
}

int main( int argc, char** argv ) {
  // declare all this stuff up here to avoid 'wild pointer' problems if jumping to fail exit on early test fails
  char *hdr_filename = NULL, *seq_filename = NULL;
  bool ret = false;

  // cube has more tests for correctness
  bool testing_default_cube = true;
  if ( argc >= 3 ) {
    hdr_filename         = argv[1];
    seq_filename         = argv[2];
    testing_default_cube = false;
  } else if ( 2 == argc ) {
    // This is really just to make life easier for AFL
    printf( "Using combined input file %s\n", argv[1] );
    testing_default_cube = false;

    hdr_filename = "header.vol";
    seq_filename = "sequence.vol";

    file_record_t file_record = ( file_record_t ){ .sz = 0 };
    if ( !_read_entire_file( argv[1], &file_record ) ) {
      fprintf( stderr, "File record could not be read.\n" );
      VOL_ASSERT( false );
    }
    {
      if ( file_record.sz <= 64 ) {
        fprintf( stderr, "File record too small for valid header + sequence\n" );
        VOL_ASSERT( false );
      }
      FILE* hdr_fp = fopen( hdr_filename, "wb" );
      VOL_ASSERT( hdr_fp );
      fwrite( file_record.byte_ptr, 64, 1, hdr_fp );
      fclose( hdr_fp );
      FILE* seq_fp = fopen( seq_filename, "wb" );
      VOL_ASSERT( seq_fp );
      fwrite( &file_record.byte_ptr[64], file_record.sz - 64, 1, seq_fp );
      fclose( seq_fp );
    }
    if ( file_record.byte_ptr ) { free( file_record.byte_ptr ); }
  } else {
    hdr_filename = "../samples/cube_hdr.vol";
    seq_filename = "../samples/cube_seq.vol";
  }
  printf( "Using input files `%s` and `%s`\n", hdr_filename, seq_filename );

  printf( "\n-----------------------------------\n\033[0;33m Starting vol_geom tests \033[0m \n-----------------------------------\n" );

  // Use custom logger to see if it works.
  vol_geom_set_log_callback( _my_logger );

  //
  // Test that attempting to free a NULL struct returns false
  //
  ret = vol_geom_free_file_info( NULL );
  VOL_ASSERT( ret == false );
  //
  // Test opening with a NULL pointer fails
  //
  ret = vol_geom_create_file_info( hdr_filename, seq_filename, NULL, false );
  VOL_ASSERT( ret == false );

  //
  // Test opening with non-existed path string fails
  //
  ret = vol_geom_create_file_info( NULL, seq_filename, &geom_info, false );
  VOL_ASSERT( ret == false );
  ret = vol_geom_create_file_info( hdr_filename, NULL, &geom_info, false );
  VOL_ASSERT( ret == false );
  ret = vol_geom_create_file_info( NULL, NULL, &geom_info, false );
  VOL_ASSERT( ret == false );
  ret = vol_geom_create_file_info( NULL, NULL, NULL, false );
  VOL_ASSERT( ret == false );

  //
  // Test opening files succeeds (assuming correct file paths were given as program args).
  //
  ret = vol_geom_create_file_info( hdr_filename, seq_filename, &geom_info, false );
  VOL_ASSERT( ret );

  { // Test properties parsed from Header are correct
    char c_str[256];
    printf( "HEADER:\n" );
    _short_str_to_c_str( c_str, &geom_info.hdr.format );
    printf( "format\t\t%s\n", c_str );
    int n = strncmp( "VOLS", c_str, 4 );
    VOL_ASSERT( 0 == n );
    printf( "version\t\t%i\n", geom_info.hdr.version );
    VOL_ASSERT( 12 == geom_info.hdr.version );
    printf( "compression\t%i\n", geom_info.hdr.compression );
    VOL_ASSERT( 0 == geom_info.hdr.compression );
    _short_str_to_c_str( c_str, &geom_info.hdr.mesh_name );
    printf( "mesh_name\t%s\n", c_str );
    _short_str_to_c_str( c_str, &geom_info.hdr.material );
    printf( "material\t%s\n", c_str );
    _short_str_to_c_str( c_str, &geom_info.hdr.shader );
    printf( "shader\t%s\n", c_str );
    printf( "topology\t%i\n", geom_info.hdr.topology );
    VOL_ASSERT( 0 == geom_info.hdr.topology );
    printf( "frame_count\t%i\n", geom_info.hdr.frame_count );
    if ( testing_default_cube ) { VOL_ASSERT( 1 == geom_info.hdr.frame_count ); }
    printf( "normals\t\t%i\n", (int)geom_info.hdr.normals );
    VOL_ASSERT( 1 == geom_info.hdr.normals );
    printf( "textured\t%i\n", (int)geom_info.hdr.textured );
    VOL_ASSERT( 0 == geom_info.hdr.textured );
    printf( "texture_width\t%i\n", (int)geom_info.hdr.texture_width );
    VOL_ASSERT( 0 == geom_info.hdr.texture_width );
    printf( "texture_height\t%i\n", (int)geom_info.hdr.texture_height );
    VOL_ASSERT( 0 == geom_info.hdr.texture_height );
    printf( "texture_format\t%i\n", (int)geom_info.hdr.texture_format );
    VOL_ASSERT( 0 == geom_info.hdr.texture_format );
    printf( "translation\t{%.2f %.2f %.2f}\n", geom_info.hdr.translation[0], geom_info.hdr.translation[1], geom_info.hdr.translation[2] );
    printf( "rotation\t{%.4f %.4f %.4f %.4f}\n", geom_info.hdr.rotation[0], geom_info.hdr.rotation[1], geom_info.hdr.rotation[2], geom_info.hdr.rotation[3] );
    printf( "scale\t\t%f\n", geom_info.hdr.scale );
    printf( "\n" );
  }

  //
  // Test reading a valid frame
  //
  {
    ret = vol_geom_read_frame( seq_filename, &geom_info, 0, &geom_frame_data );
    VOL_ASSERT( ret );
    bool is_key = vol_geom_is_keyframe( &geom_info, 0 );
    printf( "frame 0 is_keyframe = %i\n", (int)is_key );
    VOL_ASSERT( is_key );

    // NOTE(Anton) the 'short' ints used here are a brittle part of the spec - be careful!
    short* indices_short_ptr = (short*)&geom_frame_data.block_data_ptr[geom_frame_data.indices_offset];
    float* points_ptr        = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.vertices_offset];
    float* uvs_ptr           = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.uvs_offset];
    float* normals_ptr       = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.normals_offset];
    int n_vertices           = geom_frame_data.vertices_sz / sizeof( float ) * 3;

    // print some info for double-checking the data e.g. make sure this matches the original cube.obj
    printf( "n_vertices = %i\n", n_vertices );
    printf( "first 3 indices\t\t= { %i %i %i }\n", (int)indices_short_ptr[0], (int)indices_short_ptr[1], (int)indices_short_ptr[2] );
    VOL_ASSERT( 0 == indices_short_ptr[0] );
    VOL_ASSERT( 1 == indices_short_ptr[1] );
    VOL_ASSERT( 2 == indices_short_ptr[2] );
    printf( "first 3 verts\t\t= { %.2f %.2f %.2f }\n",
      points_ptr[indices_short_ptr[0] * 3 + 0], //
      points_ptr[indices_short_ptr[0] * 3 + 1], //
      points_ptr[indices_short_ptr[0] * 3 + 2]  //
    );
    printf( "first uv\t\t= { %.2f %.2f }\n",
      uvs_ptr[indices_short_ptr[0] * 2 + 0], //
      uvs_ptr[indices_short_ptr[0] * 2 + 1] );

    printf( "first normal xyz\t= { %.2f %.2f %.2f }\n",
      normals_ptr[indices_short_ptr[0] * 3 + 0], //
      normals_ptr[indices_short_ptr[0] * 3 + 1], //
      normals_ptr[indices_short_ptr[0] * 3 + 2]  //
    );

    if ( testing_default_cube ) {
      printf( "testing cube-specific values..." );
      if ( 324 != n_vertices ) { VOL_ASSERT( false ); }

      // test first 3 vertex components are correct
      if ( points_ptr[indices_short_ptr[0] * 3 + 0] >= -0.98f ) { VOL_ASSERT( false ); }
      if ( points_ptr[indices_short_ptr[0] * 3 + 0] <= -1.10f ) { VOL_ASSERT( false ); }
      if ( points_ptr[indices_short_ptr[0] * 3 + 1] <= 0.98f ) { VOL_ASSERT( false ); }
      if ( points_ptr[indices_short_ptr[0] * 3 + 1] >= 1.10f ) { VOL_ASSERT( false ); }
      if ( points_ptr[indices_short_ptr[0] * 3 + 2] >= -0.98f ) { VOL_ASSERT( false ); }
      if ( points_ptr[indices_short_ptr[0] * 3 + 2] <= -1.10f ) { VOL_ASSERT( false ); }
      // and 2 UVs components
      if ( 0.0f != uvs_ptr[indices_short_ptr[0] * 2 + 0] ) { VOL_ASSERT( false ); }
      if ( uvs_ptr[indices_short_ptr[0] * 2 + 1] <= 0.98f ) { VOL_ASSERT( false ); }
      if ( uvs_ptr[indices_short_ptr[0] * 2 + 1] >= 1.10f ) { VOL_ASSERT( false ); }
      // and 3 normals components
      if ( 0.0f != normals_ptr[indices_short_ptr[0] * 3 + 0] ) { VOL_ASSERT( false ); }
      if ( normals_ptr[indices_short_ptr[0] * 3 + 1] <= 0.98f ) { VOL_ASSERT( false ); }
      if ( normals_ptr[indices_short_ptr[0] * 3 + 1] >= 1.10f ) { VOL_ASSERT( false ); }
      if ( 0.0f != normals_ptr[indices_short_ptr[0] * 3 + 2] ) { VOL_ASSERT( false ); }
      printf( "DONE\n" );
    }
  }

  //
  // Test freeing valid info struct succeeds.
  //
  ret = vol_geom_free_file_info( &geom_info );
  VOL_ASSERT( ret );

  printf( "\n-----------------------------------\n\033[0;32m vol_geom tests passed. Normal exit \033[0m \n-----------------------------------\n" );
  return 0;
}
