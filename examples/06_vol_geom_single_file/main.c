/**
 *
 * Compile:
 gcc -g -o vol_geom_combined.bin \
 main.c \
 ../../src/vol_geom.c  -I ../../src/  \
 ../../thirdparty/apg/gfx.c ../../thirdparty/apg/apg_maths.c  -I ../../thirdparty/apg/ \
 ../../thirdparty/glad/src/glad.c  -I ../../thirdparty/glad/include/ \
 -lglfw -lm
 */

#include "gfx.h"
#include "apg_maths.h"
#include "vol_geom.h"
#include <stdint.h>
#include <stdio.h>

static void _update_mesh_with_frame( gfx_mesh_t* mesh_ptr, int frame_number, const char* filename, vol_geom_info_t* vols_info_ptr, gfx_texture_t* texture_ptr ) {
  // Read the first frame to use as the base mesh.
  vol_geom_frame_data_t vols_frame_data = ( vol_geom_frame_data_t ){ .block_data_ptr = NULL };
  if ( !vol_geom_read_frame( filename, vols_info_ptr, frame_number, &vols_frame_data ) ) {
    fprintf( stderr, "ERROR: reading frame 0 from vol sequence file\n" );
    return;
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
    size_t vols_texture_sz    = vols_frame_data.block_data_ptr[vols_frame_data.texture_sz];
    // TODO update compressed texture here
    int texture_w = 2048, texture_h = 2048, texture_n_chans = 3; // TODO get these
    gfx_update_texture( texture_ptr, vols_texture_ptr, texture_w, texture_h, texture_n_chans );
  }
}

int main( int argc, char** argv ) {
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
  gfx_texture_t texture = gfx_create_texture_from_mem( NULL, 0, 0, 3, ( gfx_texture_properties_t ){ .bilinear = true } );
  gfx_mesh_t mesh       = gfx_create_mesh_from_mem( NULL, 3, NULL, 2, NULL, 3, NULL, 0, 1, 0, true );
  _update_mesh_with_frame( &mesh, 0, filename_vols, &vols_info, &texture );
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
        _update_mesh_with_frame( &mesh, desired_keyframe, filename_vols, &vols_info, NULL ); // NULL so we don't upload a texture.
        loaded_keyframe = desired_keyframe;
      }
      _update_mesh_with_frame( &mesh, desired_frame, filename_vols, &vols_info, &texture );
      curr_frame = desired_frame;
      if ( desired_is_key ) { loaded_keyframe = desired_frame; }
    }

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
