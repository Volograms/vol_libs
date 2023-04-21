/** @file main.c Example using vol_geom with a .vols file comprising header, sequence, and Basis Universal textures.
 * Compile:
g++ -fno-strict-aliasing -DBASISD_SUPPORT_KTX2=0 \
-o basisu_transcoder.o -c ../../thirdparty/basis_universal/transcoder/basisu_transcoder.cpp -I ../../src/ -I ../../thirdparty/
gcc -o glad.o -c ../../thirdparty/glad/src/glad.c  -I ../../thirdparty/glad/include/
g++ -g -o vol_basis.o -c ../../src/vol_basis.cpp -I ../../src/ -I ../../thirdparty/
gcc -g -o vol_geom.o -c ../../src/vol_geom.c
gcc -g -o gfx.o -c ../../thirdparty/apg/gfx.c -I ../../thirdparty/glad/include/
gcc -g -o apg_maths.o -c ../../thirdparty/apg/apg_maths.c
gcc -g -o main.o -c main.c -I ../../thirdparty/apg/ -I ../../thirdparty/glad/include/ -I ../../src/
g++ -g -Wall -Werror -pedantic -o vol_geom_combined.bin \
main.o apg_maths.o gfx.o vol_geom.o vol_basis.o glad.o basisu_transcoder.o \
-lglfw -lm
*/

// TODO audio - new demo?
// TODO 1.3 support.
// TODO - sort out vol_basis and TODOs here about malloc
// TODO - UASTC support and
// TODO - switching compressed formats on the fly.
// TODO also make sure I didn't break the old split file.
// TODO think longer term about deprecating 'file info' struct and just doing:
// TODO fuzzing.
//    vol_geom_read_hdr_from_file()
//    vol_geom_read_frame_from_mem() for streaming.

#include "gfx.h"
#include "apg_maths.h"
#include "vol_basis.h"
#include "vol_geom.h"
#include "glad/glad.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Scratch memory to use for transcoding compressed textures.
#define OUTPUT_DIMS 2048
static uint8_t* output_blocks_ptr;

static void _update_compressed_texture( int level_index, uint8_t* output_blocks_ptr, uint32_t output_blocks_sz, gfx_texture_t* texture_ptr ) {
  GLenum internal_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; // NOTE DXt5 is for cTFBC3_RGBA!!!
  glBindTexture( GL_TEXTURE_2D, texture_ptr->handle_gl );
  glCompressedTexImage2D( GL_TEXTURE_2D, level_index, internal_format, texture_ptr->w, texture_ptr->h, 0, texture_ptr->w * texture_ptr->h, output_blocks_ptr );
  glBindTexture( GL_TEXTURE_2D, 0 );
}

static bool _update_mesh_with_frame( gfx_mesh_t* mesh_ptr, int frame_number, const char* filename, vol_geom_info_t* vols_info_ptr, gfx_texture_t* texture_ptr ) {
  // Read the first frame to use as the base mesh.
  vol_geom_frame_data_t vols_frame_data = ( vol_geom_frame_data_t ){ .block_data_ptr = NULL };
  if ( !vol_geom_read_frame( filename, vols_info_ptr, frame_number, &vols_frame_data ) ) {
    fprintf( stderr, "ERROR: reading frame 0 from vol sequence file\n" );
    return false;
  }
  // NOTE(Anton) the 'short' ints used here are a brittle part of the spec - be careful!
  float* points_ptr    = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.vertices_offset];
  float* uvs_ptr       = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.uvs_offset];
  uint8_t indices_type = 1; // indices_type      - Data type of indices - 0=unsigned byte, 1=unsigned short, 2=unsigned int.
  int n_vertices       = vols_frame_data.vertices_sz / ( sizeof( float ) * 3 );
  if ( vol_geom_is_keyframe( vols_info_ptr, frame_number ) ) {
    float* normals_ptr       = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.normals_offset];
    short* indices_short_ptr = (short*)&vols_frame_data.block_data_ptr[vols_frame_data.indices_offset];
    void* indices_ptr        = indices_short_ptr;          // if indices are uint32s we'd point to that here instead e.g. based on number of vertices.
    size_t indices_buffer_sz = vols_frame_data.indices_sz; //
    gfx_update_mesh_from_mem( mesh_ptr, points_ptr, 3, uvs_ptr, 2, normals_ptr, 3, indices_ptr, indices_buffer_sz, indices_type, n_vertices, true );
  } else {
    gfx_update_mesh_from_mem( mesh_ptr, points_ptr, 3, uvs_ptr, 2, NULL, 3, NULL, 0, indices_type, n_vertices, true );
  }

  if ( texture_ptr && vols_info_ptr->hdr.textured ) {
    uint8_t* vols_texture_ptr = (uint8_t*)&vols_frame_data.block_data_ptr[vols_frame_data.texture_offset];
    size_t vols_texture_sz    = vols_frame_data.texture_sz;
    if ( !vol_basis_transcode( 3, vols_texture_ptr, vols_texture_sz, output_blocks_ptr, OUTPUT_DIMS * OUTPUT_DIMS, &texture_ptr->w, &texture_ptr->h ) ) {
      fprintf( stderr, "ERROR transcoding image %i failed\n", frame_number );
      return 1;
    }
    _update_compressed_texture( 0, output_blocks_ptr, OUTPUT_DIMS * OUTPUT_DIMS, texture_ptr );
  }
  return true;
}

int main( int argc, char** argv ) {
  // TODO(Anton) there is already a block in vol_basis we could use?
  output_blocks_ptr = (uint8_t*)malloc( OUTPUT_DIMS * OUTPUT_DIMS );
  if ( !vol_basis_init() ) {
    fprintf( stderr, "ERROR vol_basis_init() failed\n" );
    return 1;
  }
  printf( "Vol basis init.\n" );

  if ( argc < 2 ) {
    printf( "Usage: %s MYFILE.vols\n", argv[0] );
    return 0;
  }
  const char* filename_vols = argv[1];
  printf( "Loading combined header&sequence file `%s`\n", filename_vols );

  // Populate the meta-data struct from a single .vols file.
  vol_geom_info_t vols_info = ( vol_geom_info_t ){ .biggest_frame_blob_sz = 0 };
  if ( !vol_geom_create_file_info_from_file( filename_vols, &vols_info ) ) {
    fprintf( stderr, "ERROR vol_geom_create_file_info_from_file.\n" );
    return 1;
  }

  gfx_start( "vol_geom Single Vols File OpenGL Example\n", 512, 512, false );
  gfx_texture_t compressed_texture;
  {
    GLuint tex_cmpr1 = 0;
    glGenTextures( 1, &tex_cmpr1 );
    GLenum internal_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    glBindTexture( GL_TEXTURE_2D, tex_cmpr1 );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glCompressedTexImage2D( GL_TEXTURE_2D, 0, internal_format, OUTPUT_DIMS, OUTPUT_DIMS, 0, OUTPUT_DIMS * OUTPUT_DIMS, NULL );
    glBindTexture( GL_TEXTURE_2D, 0 );
    compressed_texture.w = compressed_texture.h = OUTPUT_DIMS;
    compressed_texture.handle_gl                = tex_cmpr1;
    compressed_texture.n_channels               = 4;
    memset( &compressed_texture.properties, 0, sizeof( gfx_texture_properties_t ) );
  }
  gfx_mesh_t mesh = gfx_create_mesh_from_mem( NULL, 3, NULL, 2, NULL, 3, NULL, 0, 1, 0, true );
  if ( !_update_mesh_with_frame( &mesh, 0, filename_vols, &vols_info, &compressed_texture ) ) { return 1; }

  double prev_s       = gfx_get_time_s();
  double frame_s      = 0.0;
  int curr_frame      = 0;
  int loaded_keyframe = 0;
  while ( !gfx_should_window_close() ) {
    int fb_w = 0, fb_h = 0, win_w = 0, win_h = 0;
    gfx_poll_events();
    gfx_framebuffer_dims( &fb_w, &fb_h );
    if ( fb_w == 0 || fb_h == 0 ) { continue; }
    float aspect = (float)fb_w / (float)fb_h;
    gfx_window_dims( &win_w, &win_h );
    gfx_viewport( 0, 0, win_w, win_h );
    double curr_s    = gfx_get_time_s();
    double elapsed_s = curr_s - prev_s;
    prev_s           = curr_s;
    frame_s += elapsed_s;
    double spf        = 1.0 / 30.0;
    int jump_n_frames = (int)( frame_s / spf );
    frame_s -= jump_n_frames * spf;
    int desired_frame = curr_frame + jump_n_frames;
    desired_frame %= vols_info.hdr.frame_count;
    // Update mesh for new frame.
    if ( desired_frame != curr_frame ) {
      bool desired_is_key  = vol_geom_is_keyframe( &vols_info, desired_frame );
      int desired_keyframe = vol_geom_find_previous_keyframe( &vols_info, desired_frame );
      if ( desired_keyframe != loaded_keyframe && desired_keyframe != desired_frame ) {
        if ( !_update_mesh_with_frame( &mesh, desired_keyframe, filename_vols, &vols_info, NULL ) ) { return 1; } // NULL so we don't upload a texture.
        loaded_keyframe = desired_keyframe;
      }
      if ( !_update_mesh_with_frame( &mesh, desired_frame, filename_vols, &vols_info, &compressed_texture ) ) { return 1; }
      curr_frame = desired_frame;
      if ( desired_is_key ) { loaded_keyframe = desired_frame; }
    }

    // create camera view and perspective matrices
    // look at cube corner rather than front-on, and hover a bit above so we get a 3D impression.
    mat4 P = perspective( 66.6f, aspect, 0.1f, 100.0f );
    mat4 M = scale_mat4( ( vec3 ){ -1, 1, 1 } );
    mat4 V = look_at( ( vec3 ){ 0, 1, 2 }, ( vec3 ){ 0, 1, 0 }, ( vec3 ){ 0, 1, 0 } );
    gfx_clear_colour_and_depth_buffers( 0.5f, 0.5f, 0.5f, 1.0f );

    gfx_winding_cw( true );
    gfx_draw_mesh( mesh, GFX_PT_TRIANGLES, gfx_default_textured_shader, P.m, V.m, M.m, &compressed_texture, 1 );
    gfx_winding_cw( false );

    gfx_swap_buffer();
  }

  gfx_delete_mesh( &mesh );
  gfx_stop();

  if ( !vol_geom_free_file_info( &vols_info ) ) { fprintf( stderr, "ERROR: freeing vol info\n" ); }

  if ( !vol_basis_free() ) { fprintf( stderr, "ERROR: vol_basis_free\n" ); }

  if ( output_blocks_ptr ) { free( output_blocks_ptr ); }
  printf( "Normal exit.\n" );
  return 0;
}
