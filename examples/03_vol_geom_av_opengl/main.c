/** @file main.c Example using vol_geom with OpenGL
 * Authors:   Anton Gerdelan <anton@volograms.com>
 * Copyright: 2021, Volograms (http://volograms.com/)
 * Language:  C99
 * Licence:   The MIT License. See LICENSE.md for details.
 *
 * Hit the SPACEBAR to advance 1 frame.
 * Hit 'P' to play/pause the vologram.
 * This example is set up to loop the vologram animation by default.
 *
 * You can load a vologram like this:
 * ./vol_geom_av_opengl.bin HEADER.VOLS SEQUENCE.VOLS [TEXTURE.MP4]
 *
 * If no vologram files are specified on the command line then a default quad+counter texture will be used.
 * This only has 1 geometry frame so it always reads "000".
 */

#include "gfx.h"
#include "apg_maths.h"
#include "vol_av.h"
#include "vol_geom.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vol_av_video_t av_info;
static vol_geom_info_t geom_info;
static gfx_texture_t texture;
static gfx_mesh_t mesh;

static bool loop_vologram           = true;
static char* default_vol_hdr_file   = "../samples/cube_hdr.vol";
static char* default_vol_seq_file   = "../samples/cube_seq.vol";
static char* default_vol_video_file = "../samples/counter.mp4";

// fetch the subsequent frame from the video as an image, and update the texture with it.
static bool _next_video_frame_to_texture() {
  if ( !vol_av_read_next_frame( &av_info ) ) {
    fprintf( stderr, "ERROR: reading frame from vol video file\n" );
    return false;
  }
  if ( !av_info.pixels_ptr ) {
    fprintf( stderr, "ERROR: pixels pointer NULL at  frame number 0\n" );
    return false;
  }
  gfx_update_texture( &texture, av_info.pixels_ptr, av_info.w, av_info.h, 3 );
  if ( !texture.handle_gl ) {
    fprintf( stderr, "ERROR creating texture\n" );
    return 1;
  }
  return true;
}

vol_geom_frame_data_t geom_frame_data;

// fetch a given frame from the vologram sequence, and update the vertex buffers (mesh) with it.
static bool _update_mesh_with_frame( const char* vol_seq_file, int frame_idx ) {
  if ( !vol_geom_read_frame( vol_seq_file, &geom_info, frame_idx, &geom_frame_data ) ) {
    fprintf( stderr, "ERROR: reading frame %i from vol sequence file\n", frame_idx );
    return false;
  }
  bool is_key = geom_info.frame_headers_ptr[frame_idx].keyframe;
  printf( "frame %i is_keyframe = %i\n", frame_idx, (int)is_key );

  // NOTE(Anton) the 'short' ints used here are a brittle part of the spec - be careful!
  short* indices_short_ptr = (short*)&geom_frame_data.block_data_ptr[geom_frame_data.indices_offset];
  float* points_ptr        = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.vertices_offset];
  float* uvs_ptr           = NULL;
  float* normals_ptr       = NULL;
  int n_vertices           = geom_frame_data.vertices_sz / ( sizeof( float ) * 3 );
  void* indices_ptr        = NULL;                       // if indices are uint32s we'd point to that here instead e.g. based on number of vertices.
  size_t indices_buffer_sz = geom_frame_data.indices_sz; //
  // indices_type      - Data type of indices - 0=unsigned byte, 1=unsigned short, 2=unsigned int.
  uint8_t indices_type = n_vertices >= 65535 ? 2 : 1; // uint (2), or ushort (1) is used for small meshes (or old versions of Unity).

  // print some info for double-checking the data e.g. make sure this matches the original cube.obj
  printf( "n_vertices = %i\n", n_vertices );
  if ( is_key ) {
    printf( "first 3 verts       = { %.2f %.2f %.2f }\n",
      points_ptr[indices_short_ptr[0] * 3 + 0], // note reversal of winding order required from CW to CCW
      points_ptr[indices_short_ptr[0] * 3 + 1], //
      points_ptr[indices_short_ptr[0] * 3 + 2]  //
    );
  }

  if ( geom_info.frame_headers_ptr[frame_idx].keyframe ) {
    printf( "frame %i keyframe detected, specifying UVs and Indices\n", frame_idx );
    uvs_ptr = (float*)&geom_frame_data.block_data_ptr[geom_frame_data.uvs_offset];
    if ( geom_info.hdr.normals ) {
      assert( (size_t)geom_frame_data.normals_sz == 3 * sizeof( float ) * n_vertices );
      normals_ptr = (float*)( &geom_frame_data.block_data_ptr[geom_frame_data.normals_offset] );
    }
    indices_ptr = &geom_frame_data.block_data_ptr[geom_frame_data.indices_offset];
    printf( "first 3 indices     = { %i %i %i }\n", (int)indices_short_ptr[0], (int)indices_short_ptr[1], (int)indices_short_ptr[2] );
  }

  // copy vertex data to OpenGL buffer (if >1 frame we would update this mesh data every vologram frame)
  gfx_update_mesh_from_mem( &mesh, points_ptr, 3, uvs_ptr, 2, normals_ptr, 3, indices_ptr, indices_buffer_sz, indices_type, n_vertices, true );
  if ( 0 == mesh.vao ) {
    fprintf( stderr, "ERROR updating mesh - vao=0\n" );
    return false;
  }

  return true;
}

int main( int argc, char** argv ) {
  char* vol_hdr_file   = default_vol_hdr_file;
  char* vol_seq_file   = default_vol_seq_file;
  char* vol_video_file = default_vol_video_file;
  if ( argc >= 3 ) {
    vol_hdr_file = argv[1];
    vol_seq_file = argv[2];
  } else {
    printf( "No volograms specified as arguments - using default cube files\n" );
  }

  if ( argc >= 4 ) {
    vol_video_file = argv[3];
  } else {
    printf( "No video texture file specified as argument - using a default video\n" );
  }
  printf( "Vologram header file   = `%s`\n", vol_hdr_file );
  printf( "Vologram sequence file = `%s`\n", vol_seq_file );
  printf( "Vologram video texture file = `%s`\n", vol_video_file );

  // start OpenGL
  gfx_start( "vol_geom OpenGL Example\n", 512, 512, false );
  printf( "opengl context started.\n" );

  if ( !vol_av_open( vol_video_file, &av_info ) ) {
    fprintf( stderr, "ERROR: opening vol video file `%s`\n", vol_video_file );
    return 1;
  }

  // Builds a little database of what is in the files (but doesn't keep files open).
  if ( !vol_geom_create_file_info( vol_hdr_file, vol_seq_file, &geom_info, false ) ) {
    fprintf( stderr, "ERROR: opening vol files `%s`, `%s`\n", vol_hdr_file, vol_seq_file );
    return 1;
  }

  // create empty texture
  texture = gfx_create_texture_from_mem( NULL, 1024, 1024, 3, ( gfx_texture_properties_t ){ .bilinear = true, .has_mips = true, .is_srgb = true } );
  // create empty mesh
  mesh = gfx_create_mesh_from_mem( NULL, 3, NULL, 2, NULL, 3, NULL, 0, 0, 0, true );

  // READ FRAME 0
  {
    _update_mesh_with_frame( vol_seq_file, 0 );
    // and then fill from first video frame
    _next_video_frame_to_texture();
  }

  // create a shader to use
  // a string for the vertex shader
  const char* vs_str =
    "#version 410\n"
    "in vec3 a_vp;\n"
    "in vec2 a_vt;\n"
    "in vec3 a_vn;\n"
    "uniform mat4 u_P, u_V, u_M;\n"
    "out vec2 v_vt;\n"
    "out vec3 v_vn;\n"
    "void main() {\n"
    "  v_vt = vec2( a_vt.s, 1.0 - a_vt.t );\n"
    "  v_vn = a_vn;\n"
    // important note: vologram geometry is in Unity mesh format so -x and clockwise ordering.
    "  gl_Position = u_P * u_V * u_M * vec4( -a_vp.x, a_vp.y, a_vp.z, 1.0 );\n"
    "}\n";

  // a string for the fragment shader
  const char* fs_str =
    "#version 410\n"
    "in vec2 v_vt;\n"
    "in vec3 v_vn;\n"
    "uniform sampler2D u_texture_a;\n"
    "out vec4 o_frag_colour;\n"
    "void main() {\n"
    "  vec4 rgba = texture( u_texture_a, v_vt );\n"
    "  o_frag_colour = vec4( rgba.rgb, 1.0 );\n"
    "}\n"; // TODO gamma

  gfx_shader_t shader = gfx_create_shader_program_from_strings( vs_str, fs_str );

  // spacebar to advance frame
  int curr_frame_idx  = 0;
  bool spacebar_down  = input_is_key_held( ' ' );
  bool space_was_down = spacebar_down;
  bool p_down         = input_is_key_held( 'P' );
  bool p_was_down     = p_down;
  bool playing        = false;
  double fps          = vol_av_frame_rate( &av_info );
  double prev_time_s  = gfx_get_time_s();
  printf( "fps=%f\n", fps );
  printf( "curr_frame_idx=%i\n", curr_frame_idx );

  int n_vid_frames  = vol_av_frame_count( &av_info );
  int n_geom_frames = geom_info.hdr.frame_count;
  // use the shorter of the 2 durations for any playback cut-off
  int n_frames = n_vid_frames < n_geom_frames ? n_vid_frames : n_geom_frames;
  printf( "n_vid_frames=%i, n_geom_frames=%i\n", n_vid_frames, n_geom_frames );

  double frame_timer_s = 0.0;
  assert( fps > 0.0 );
  double spf = 1.0 / fps;
  while ( !gfx_should_window_close() ) {
    gfx_poll_events();
    double curr_time_s = gfx_get_time_s();
    double elapsed_s   = curr_time_s - prev_time_s;
    prev_time_s        = curr_time_s;

    bool advance_frame = false;

    if ( playing ) {
      frame_timer_s += elapsed_s;
      if ( frame_timer_s >= spf ) {
        frame_timer_s -= spf;
        advance_frame = true;
      }
    }

    p_down = input_is_key_held( 'P' );
    if ( p_down && !p_was_down ) { playing = !playing; }
    p_was_down = p_down;

    spacebar_down = input_is_key_held( ' ' );
    if ( spacebar_down && !space_was_down ) { advance_frame = true; }
    space_was_down = spacebar_down;

    if ( advance_frame ) {
      if ( curr_frame_idx < n_frames - 1 ) {
        curr_frame_idx++;
        printf( "curr_frame_idx=%i / %i\n", curr_frame_idx, n_frames );
        _next_video_frame_to_texture();
        _update_mesh_with_frame( vol_seq_file, curr_frame_idx );
      } else if ( loop_vologram ) {
        curr_frame_idx = 0;
        printf( "LOOPING. curr_frame_idx=%i / %i\n", curr_frame_idx, n_frames );
        // since ffmpeg seek isn't reliable: a slow-hacky way to restart the video:
        if ( !vol_av_close( &av_info ) ) {
          fprintf( stderr, "ERROR: opening vol video file `%s`\n", vol_video_file );
          return 1;
        }
        if ( !vol_av_open( vol_video_file, &av_info ) ) {
          fprintf( stderr, "ERROR: opening vol video file `%s`\n", vol_video_file );
          return 1;
        }
        _next_video_frame_to_texture();
        _update_mesh_with_frame( vol_seq_file, curr_frame_idx );
      }
    }

    int fb_w = 0, fb_h = 0;
    gfx_framebuffer_dims( &fb_w, &fb_h );
    if ( fb_w == 0 || fb_h == 0 ) { continue; }
    float aspect = (float)fb_w / (float)fb_h;

    // create camera view and perspective matrices
    // look at cube corner rather than front-on, and hover a bit above so we get a 3D impression.
    mat4 V = look_at( ( vec3 ){ 0.0f, 1.0f, 2.0f }, ( vec3 ){ 0.0f, 1.0f, 0.0f }, ( vec3 ){ 0.0f, 1.0f, 0.0f } );
    mat4 P = perspective( 66.6f, aspect, 0.1f, 100.0f );
    mat4 M = rot_y_deg_mat4( 0.0f );

    gfx_viewport( 0, 0, fb_w, fb_h );
    gfx_clear_colour_and_depth_buffers( 0.5f, 0.5f, 0.5f, 1.0f );

    gfx_depth_testing( true );
    gfx_winding_cw( true ); // NOTE: Unity-style mesh winding used.

    gfx_draw_mesh( mesh, GFX_PT_TRIANGLES, shader, P.m, V.m, M.m, &texture, 1 );

    gfx_swap_buffer();
  }

  gfx_delete_shader_program( &shader );
  gfx_delete_mesh( &mesh );
  gfx_delete_texture( &texture );
  gfx_stop();

  {
    if ( !vol_geom_free_file_info( &geom_info ) ) {
      fprintf( stderr, "ERROR: freeing vol info\n" );
      return 1;
    }
  }

  if ( !vol_av_close( &av_info ) ) {
    fprintf( stderr, "ERROR: freeing av info\n" );
    return 1;
  }

  printf( "normal exit\n" );
  return 0;
}
