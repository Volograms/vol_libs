/** @file main.c Example using vol_geom with OpenGL
 * Authors:   Anton Gerdelan <anton@volograms.com>
 * Copyright: 2021, Volograms (http://volograms.com/)
 * Language:  C99
 * Licence:   The MIT License. See LICENSE.md for details.
 */

#include "gfx.h"
#include "apg_maths.h"
#include "vol_geom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* default_hdr_file = "../samples/cube_hdr.vol";
static char* default_seq_file = "../samples/cube_seq.vol";

int main( int argc, char** argv ) {
  char* vol_hdr_file = default_hdr_file;
  char* vol_seq_file = default_seq_file;
  if ( argc >= 3 ) {
    vol_hdr_file = argv[1];
    vol_seq_file = argv[2];
  } else {
    printf( "No volograms specified as arguments - using default cube files\n" );
  }
  printf( "Vologram header file   = `%s`\n", vol_hdr_file );
  printf( "Vologram sequence file = `%s`\n", vol_seq_file );

  vol_geom_info_t geom_info             = ( vol_geom_info_t ){ .hdr.compression = 0 };
  vol_geom_frame_data_t geom_frame_data = ( vol_geom_frame_data_t ){ .block_data_ptr = NULL };

  // Builds a little database of what is in the files (but doesn't keep files open).
  bool streaming_mode = false; // Note - usually we want this to be _true_, but it can be more efficient to set this
                               // to _false_ for small volograms, which loads the entire sequence into memory.
  if ( !vol_geom_create_file_info( vol_hdr_file, vol_seq_file, &geom_info, streaming_mode ) ) {
    fprintf( stderr, "ERROR: opening vol files `%s`, `%s`\n", vol_hdr_file, vol_seq_file );
    return 1;
  }

  if ( !vol_geom_read_frame( vol_seq_file, &geom_info, 0, &geom_frame_data ) ) {
    fprintf( stderr, "ERROR: reading frame 0 from vol sequence file\n" );
    return 1;
  }
  bool is_key = vol_geom_is_keyframe( &geom_info, 0 );
  printf( "frame 0 is_keyframe = %i\n", (int)is_key );

  // NOTE(Anton) the 'short' ints used here are a brittle part of the spec - be careful!
  short* indices_short_ptr = (short*)&geom_frame_data.block_data_ptr[geom_frame_data.indices_offset];
  float* points_ptr        = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.vertices_offset];
  float* uvs_ptr           = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.uvs_offset];
  float* normals_ptr       = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.normals_offset];
  int n_vertices           = geom_frame_data.vertices_sz / ( sizeof( float ) * 3 );
  void* indices_ptr        = indices_short_ptr;          // if indices are uint32s we'd point to that here instead e.g. based on number of vertices.
  size_t indices_buffer_sz = geom_frame_data.indices_sz; //
  uint8_t indices_type     = 1;                          // indices_type      - Data type of indices - 0=unsigned byte, 1=unsigned short, 2=unsigned int.

  // print some info for double-checking the data e.g. make sure this matches the original cube.obj
  printf( "n_vertices = %i\n", n_vertices );
  printf( "first 3 indices     = { %i %i %i }\n", (int)indices_short_ptr[0], (int)indices_short_ptr[1], (int)indices_short_ptr[2] );
  printf( "first 3 verts       = { %.2f %.2f %.2f }\n",
    points_ptr[indices_short_ptr[0] * 3 + 0], //
    points_ptr[indices_short_ptr[0] * 3 + 1], //
    points_ptr[indices_short_ptr[0] * 3 + 2]  //
  );

  // start OpenGL
  gfx_start( "vol_geom OpenGL Example\n", 512, 512, false );

  // copy vertex data to OpenGL buffer (if >1 frame we would update this mesh data every vologram frame)
  gfx_mesh_t mesh = gfx_create_mesh_from_mem( points_ptr, 3, uvs_ptr, 2, normals_ptr, 3, indices_ptr, indices_buffer_sz, indices_type, n_vertices, true );

  // create a shader to use

  // a string for the vertex shader
  const char* vs_str =
    "#version 410\n"
    "in vec3 a_vp;\n"
    "in vec2 a_vt;\n"
    "in vec3 a_vn;\n"
    "uniform mat4 u_P, u_V, u_M;\n"
    "out vec3 v_vn;\n"
    "void main() {\n"
    "  v_vn = a_vn;\n"
    "  gl_Position = u_P * u_V * u_M * vec4( a_vp, 1.0 );\n"
    "}\n";

  // a string for the fragment shader
  const char* fs_str =
    "#version 410\n"
    "in vec3 v_vn;\n"
    "out vec4 o_frag_colour;\n"
    "void main() {\n"
    "  o_frag_colour =vec4( abs( v_vn ), 1.0 );\n"
    "}\n";

  gfx_shader_t shader = gfx_create_shader_program_from_strings( vs_str, fs_str );

  while ( !gfx_should_window_close() ) {
    gfx_poll_events();

    int fb_w = 0, fb_h = 0;
    gfx_framebuffer_dims( &fb_w, &fb_h );
    if ( fb_w == 0 || fb_h == 0 ) { continue; }
    float aspect = (float)fb_w / (float)fb_h;

    // create camera view and perspective matrices
    // look at cube corner rather than front-on, and hover a bit above so we get a 3D impression.
    mat4 V = look_at( ( vec3 ){ 5.0f, 2.0f, -5.0f }, ( vec3 ){ 0.0f, 2.0f, 0.0f }, ( vec3 ){ 0.0f, 1.0f, 0.0f } );
    mat4 P = perspective( 66.6f, aspect, 0.1f, 100.0f );
    mat4 M = identity_mat4();

    gfx_clear_colour_and_depth_buffers( 0.5f, 0.5f, 0.5f, 1.0f );

    gfx_draw_mesh( mesh, GFX_PT_TRIANGLES, shader, P.m, V.m, M.m, NULL, 0 );

    gfx_swap_buffer();
  }

  gfx_delete_mesh( &mesh );
  gfx_stop();

  if ( !vol_geom_free_file_info( &geom_info ) ) {
    fprintf( stderr, "ERROR: freeing vol info\n" );
    return 1;
  }

  printf( "normal exit\n" );
  return 0;
}
