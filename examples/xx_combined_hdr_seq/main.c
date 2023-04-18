/**
 *
 * Compile:
 gcc -g main.c \
 ../../src/vol_geom.c  -I ../../src/  \
 ../../thirdparty/apg/gfx.c ../../thirdparty/apg/apg_maths.c  -I ../../thirdparty/apg/ \
 ../../thirdparty/glad/src/glad.c  -I ../../thirdparty/glad/include/ \
 -lglfw -lm
 */

#include "gfx.h"
#include "apg_maths.h"
#include "vol_geom.h"
#include <stdio.h>

int main( int argc, char** argv ) {
  gfx_texture_t texture;
  gfx_mesh_t mesh;
  gfx_shader_t shader;
  bool loop_vologram = true;

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

  {
    // Read the first frame to use as the base mesh.
    vol_geom_frame_data_t vols_frame_data = ( vol_geom_frame_data_t ){ .block_data_ptr = NULL };
    if ( !vol_geom_read_frame( filename_vols, &vols_info, 0, &vols_frame_data ) ) {
      fprintf( stderr, "ERROR: reading frame 0 from vol sequence file\n" );
      return 1;
    }
    // NOTE(Anton) the 'short' ints used here are a brittle part of the spec - be careful!
    short* indices_short_ptr = (short*)&vols_frame_data.block_data_ptr[vols_frame_data.indices_offset];
    float* points_ptr        = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.vertices_offset];
    float* uvs_ptr           = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.uvs_offset];
    float* normals_ptr       = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.normals_offset];
    int n_vertices           = vols_frame_data.vertices_sz / ( sizeof( float ) * 3 );
    void* indices_ptr        = indices_short_ptr;          // if indices are uint32s we'd point to that here instead e.g. based on number of vertices.
    size_t indices_buffer_sz = vols_frame_data.indices_sz; //
    uint8_t indices_type     = 1;                          // indices_type      - Data type of indices - 0=unsigned byte, 1=unsigned short, 2=unsigned int.
    mesh = gfx_create_mesh_from_mem( points_ptr, 3, uvs_ptr, 2, normals_ptr, 3, indices_ptr, indices_buffer_sz, indices_type, n_vertices, true );
  }
  { // Basic shaders.
    const char* vs_str =
      "#version 410\n"
      "in vec3 a_vp;\n"
      "in vec2 a_vt;\n"
      "in vec3 a_vn;\n"
      "uniform mat4 u_P, u_V, u_M;\n"
      "out vec3 v_vn_wor;\n"
      "void main() {\n"
      "  v_vn_wor = ( u_M * vec4( vec3( -a_vn.x, a_vn.yz ), 0.0 ) ).xyz;\n"
      "  gl_Position = u_P * u_V * u_M * vec4( -a_vp.x, a_vp.yz, 1.0 );\n"
      "}\n";
    const char* fs_str =
      "#version 410\n"
      "in vec3 v_vn_wor;\n"
      "out vec4 o_frag_colour;\n"
      "void main() {\n"
      "  o_frag_colour = vec4( v_vn_wor, 1.0 );\n"
      "}\n";
    shader = gfx_create_shader_program_from_strings( vs_str, fs_str );
  }

  // TODO play the contents - see if it works with my cat file and an official combined v1.2 file.
  // TODO also make sure I didn't break the old split file.
  // TODO think longer term about deprecating 'file info' struct and just doing:
  //    vol_geom_read_hdr_from_file()
  //    vol_geom_read_frame_from_mem() for streaming.

  while ( !gfx_should_window_close() ) {
    int fb_w = 0, fb_h = 0, win_w = 0, win_h = 0;
    gfx_poll_events();
    gfx_framebuffer_dims( &fb_w, &fb_h );
    if ( fb_w == 0 || fb_h == 0 ) { continue; }
    float aspect = (float)fb_w / (float)fb_h;
    gfx_window_dims( &win_w, &win_h );
    gfx_viewport( 0, 0, win_w, win_h );

    // create camera view and perspective matrices
    // look at cube corner rather than front-on, and hover a bit above so we get a 3D impression.
    mat4 V = look_at( ( vec3 ){ 0.0f, 1.0f, 2.0f }, ( vec3 ){ 0.0f, 1.0f, 0.0f }, ( vec3 ){ 0.0f, 1.0f, 0.0f } );
    mat4 P = perspective( 66.6f, aspect, 0.1f, 100.0f );
    mat4 M = identity_mat4();

    gfx_clear_colour_and_depth_buffers( 0.5f, 0.5f, 0.5f, 1.0f );

    gfx_winding_cw( true );
    gfx_draw_mesh( mesh, GFX_PT_TRIANGLES, shader, P.m, V.m, M.m, NULL, 0 );
    gfx_winding_cw( false );

    gfx_swap_buffer();
  }

  gfx_delete_mesh( &mesh );
  gfx_stop();

  if ( !vol_geom_free_file_info( &vols_info ) ) {
    fprintf( stderr, "ERROR: freeing vol info\n" );
    return 1;
  }

  printf( "Normal exit.\n" );
  return 0;
}
