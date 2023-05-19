/** @file main.c Example using vol_geom with a .vols file comprising header, sequence, and Basis Universal textures.
 * Authors:   Anton Gerdelan <anton@volograms.com>
 * Copyright: 2023, Volograms (http://volograms.com/)
 * Language:  C99
 * Licence:   The MIT License. See LICENSE.md for details.
 *
 * You can load a vologram like this:
 * ./vol_geom_single_file.bin FILE.vols
 * If no file is specified a default file will load.
 */

/*
TODO
3. fix the relative paths for inputs
*/

#include "gfx.h"
#include "apg_maths.h"
#include "vol_basis.h"
#include "vol_geom.h"
#include "glad/glad.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// 1-5.
static int n_volograms = 1;

ma_engine engine[5]; // Each playing sound seems to need its own 'engine' in miniaudio.
ma_sound sounds[5];  // Made these global to simplify memory/pointer setup.

/// Scratch memory to use for transcoding compressed textures.
#define OUTPUT_DIMS 2048
static uint8_t* output_blocks_ptr;

/// Convenience struct to hold all the variables for playing an unique vologram file.
typedef struct vologram_t {
  char filename_vols[1024];  // e.g. "combined.vols"
  vol_geom_info_t vols_info; // Meta-data loaded from the VOLS file.
  gfx_texture_t texture;     // OpenGL texture details.
  gfx_mesh_t mesh;           // OpenGL geometry buffers.
  ma_sound* sound_ptr;
  ma_engine* engine_ptr;
  double fps;               // Frames-per-second to play the animation.
  double frame_s;           // Playback time elapsed since the current frame started, in seconds.
  uint32_t curr_frame;      // Index of the current vologram animation frame. Starting from 0.
  uint32_t loaded_keyframe; // Index of the most recently loaded keyframe. Starting from 0.
  vec3 pos_wor;             // "World" position of the vologram in the 3D scene.
  bool loaded;              // True if the vologram was loaded and initialised successfully.
} vologram_t;

static void _update_compressed_texture( int level_index, uint8_t* output_blocks_ptr, gfx_texture_t* texture_ptr ) {
  GLenum internal_format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; // NOTE DXt5 is for cTFBC3_RGBA.
  glBindTexture( GL_TEXTURE_2D, texture_ptr->handle_gl );
  glCompressedTexImage2D( GL_TEXTURE_2D, level_index, internal_format, texture_ptr->w, texture_ptr->h, 0, texture_ptr->w * texture_ptr->h, output_blocks_ptr );
  glBindTexture( GL_TEXTURE_2D, 0 );
}

/** Update the OpenGL geometry and texture with the given vologram frame number.
 *  This function does not check if the corresponding keyframe was loaded first.
 */
static bool _update_mesh_with_frame( gfx_mesh_t* mesh_ptr, uint32_t frame_number, const char* filename, vol_geom_info_t* vols_info_ptr, gfx_texture_t* texture_ptr ) {
  vol_geom_frame_data_t vols_frame_data = ( vol_geom_frame_data_t ){ .block_data_ptr = NULL };
  if ( !vol_geom_read_frame( filename, vols_info_ptr, frame_number, &vols_frame_data ) ) {
    fprintf( stderr, "ERROR: reading frame 0 from vol sequence file\n" );
    return false;
  }
  float* points_ptr    = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.vertices_offset];
  float* uvs_ptr       = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.uvs_offset];
  uint8_t indices_type = 1; // indices_type - Data type of indices - { 0=unsigned byte, 1=unsigned short, 2=unsigned int }.
  int n_vertices       = vols_frame_data.vertices_sz / ( sizeof( float ) * 3 );
  bool is_key          = vol_geom_is_keyframe( vols_info_ptr, frame_number );
  if ( is_key ) {
    float* normals_ptr       = (float*)&vols_frame_data.block_data_ptr[vols_frame_data.normals_offset];
    short* indices_short_ptr = (short*)&vols_frame_data.block_data_ptr[vols_frame_data.indices_offset];
    void* indices_ptr        = indices_short_ptr;          //
    size_t indices_buffer_sz = vols_frame_data.indices_sz; //
    gfx_update_mesh_from_mem( mesh_ptr, points_ptr, 3, uvs_ptr, 2, normals_ptr, 3, indices_ptr, indices_buffer_sz, indices_type, n_vertices, true );
  } else {
    gfx_update_mesh_from_mem( mesh_ptr, points_ptr, 3, NULL, 2, NULL, 3, NULL, 0, indices_type, n_vertices, true );
  }

  if ( texture_ptr && vols_info_ptr->hdr.textured ) {
    uint8_t* vols_texture_ptr = (uint8_t*)&vols_frame_data.block_data_ptr[vols_frame_data.texture_offset];
    size_t vols_texture_sz    = vols_frame_data.texture_sz;
    if ( !vol_basis_transcode( 3, vols_texture_ptr, vols_texture_sz, output_blocks_ptr, OUTPUT_DIMS * OUTPUT_DIMS, &texture_ptr->w, &texture_ptr->h ) ) {
      fprintf( stderr, "ERROR transcoding image %i failed\n", frame_number );
      return 1;
    }
    _update_compressed_texture( 0, output_blocks_ptr, texture_ptr );
  }
  return true;
}

/** Update the OpenGL geometry and texture with the desired vologram frame number.
 *  This function also checks if the corresponding keyframe was loaded, and if not, loads that in first.
 */
static bool _update_mesh_new_frame( gfx_mesh_t* mesh_ptr, gfx_texture_t* compressed_texture_ptr, uint32_t desired_frame, uint32_t* curr_frame_ptr,
  uint32_t loaded_keyframe, vol_geom_info_t* vols_info_ptr, const char* filename_vols ) {
  if ( desired_frame != *curr_frame_ptr ) {
    bool desired_is_key       = vol_geom_is_keyframe( vols_info_ptr, desired_frame );
    uint32_t desired_keyframe = vol_geom_find_previous_keyframe( vols_info_ptr, desired_frame );
    if ( desired_keyframe != loaded_keyframe && desired_keyframe != desired_frame ) {
      if ( !_update_mesh_with_frame( mesh_ptr, desired_keyframe, filename_vols, vols_info_ptr, NULL ) ) { return false; } // NULL so we don't upload a texture.
      loaded_keyframe = desired_keyframe;
    }
    if ( !_update_mesh_with_frame( mesh_ptr, desired_frame, filename_vols, vols_info_ptr, compressed_texture_ptr ) ) { return false; }
    *curr_frame_ptr = desired_frame;
    if ( desired_is_key ) { loaded_keyframe = desired_frame; }
  }
  return true;
}

/** Load and initialise an unique vologram.
 *  On success the returned vologram struct will have its loaded flag set to true.
 */
static vologram_t _create_vologram( const char* filename, vec3 pos_wor, uint32_t starting_frame, ma_sound* sound_ptr, ma_engine* engine_ptr ) {
  vologram_t v =
    ( vologram_t ){ .loaded = false, .sound_ptr = sound_ptr, .engine_ptr = engine_ptr, .pos_wor = pos_wor, .curr_frame = starting_frame, .loaded_keyframe = -1 };
  strcpy( v.filename_vols, filename );

  // Populate the meta-data struct from a single .vols file.
  if ( !vol_geom_create_file_info_from_file( v.filename_vols, &v.vols_info ) ) {
    fprintf( stderr, "ERROR vol_geom_create_file_info_from_file.\n" );
    return v;
  }
  v.fps      = v.vols_info.hdr.fps ? v.vols_info.hdr.fps : 30.0;
  double spf = 1.0 / v.fps;
  v.frame_s  = spf * (double)starting_frame;
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
    v.texture.w = v.texture.h = OUTPUT_DIMS;
    v.texture.handle_gl       = tex_cmpr1;
    v.texture.n_channels      = 4;
    memset( &v.texture.properties, 0, sizeof( gfx_texture_properties_t ) );
  }
  v.mesh = gfx_create_mesh_from_mem( NULL, 3, NULL, 2, NULL, 3, NULL, 0, 1, 0, true );
  if ( !_update_mesh_with_frame( &v.mesh, v.curr_frame, v.filename_vols, &v.vols_info, &v.texture ) ) {
    fprintf( stderr, "update mesh with frame failed\n" );
    return v;
  }

  if ( v.vols_info.hdr.audio ) {
    // Note(Anton) This is a hack because miniaudio's file API is way simpler than the from-data decoder API.
    FILE* f_ptr = fopen( "audiofile", "wb" );
    fwrite( v.vols_info.audio_data_ptr, v.vols_info.audio_data_sz, 1, f_ptr );
    fclose( f_ptr );

    ma_sound_init_from_file( engine_ptr, "audiofile", 0, NULL, NULL, v.sound_ptr );
    double pcm = v.frame_s * (double)ma_engine_get_sample_rate( ma_sound_get_engine( v.sound_ptr ) );
    ma_sound_seek_to_pcm_frame( v.sound_ptr, pcm ); // Rewind to start.
    ma_sound_start( v.sound_ptr );
  }

  v.loaded = true;
  return v;
}

static void _update_vologram( double elapsed_s, vologram_t* v_ptr ) {
  v_ptr->frame_s += elapsed_s;
  double spf        = 1.0 / v_ptr->fps;
  int jump_n_frames = (int)( v_ptr->frame_s / spf );
  v_ptr->frame_s -= jump_n_frames * spf;
  uint32_t desired_frame = v_ptr->curr_frame + jump_n_frames;
  if ( desired_frame >= v_ptr->vols_info.hdr.frame_count ) {
    desired_frame = 0;
    ma_sound_stop( v_ptr->sound_ptr );
    ma_sound_seek_to_pcm_frame( v_ptr->sound_ptr, 0 ); // Rewind to start.
    ma_sound_start( v_ptr->sound_ptr );
  }
  if ( desired_frame == v_ptr->curr_frame ) { return; }
  _update_mesh_new_frame( &v_ptr->mesh, &v_ptr->texture, desired_frame, &v_ptr->curr_frame, v_ptr->loaded_keyframe, &v_ptr->vols_info, v_ptr->filename_vols );
}

static void _draw_vologram( mat4 P, mat4 V, vologram_t* v_ptr ) {
  mat4 T = translate_mat4( v_ptr->pos_wor );
  mat4 S = scale_mat4( ( vec3 ){ .x = -1.0f, .y = 1.0f, .z = 1.0f } );
  mat4 M = mult_mat4_mat4( T, S );
  gfx_winding_cw( true );
  gfx_draw_mesh( v_ptr->mesh, GFX_PT_TRIANGLES, gfx_default_textured_shader, P.m, V.m, M.m, &v_ptr->texture, 1 );
  gfx_winding_cw( false );
}

static bool _audio_start( void ) {
  for ( int i = 0; i < n_volograms; i++ ) {
    ma_result result = ma_engine_init( NULL, &engine[i] );
    if ( result != MA_SUCCESS ) { return false; }
  }
  return true;
}

int main( int argc, char** argv ) {
  const char* filename_vols = argv[1];
  if ( argc < 2 ) {
    filename_vols = "../samples/combined.vols";
    printf( "Usage: %s MYFILE.vols [#VOLOGRAMS]\n. Using default: %s", argv[0], filename_vols );
  }
  if ( argc > 2 ) { n_volograms = atoi( argv[2] ); }

  gfx_start( "vol_geom Single Vols File OpenGL Example\n", 512, 512, false );
  if ( !_audio_start() ) { return 1; }

  printf( "Init vol_basis...\n" );
  output_blocks_ptr = (uint8_t*)malloc( OUTPUT_DIMS * OUTPUT_DIMS );
  if ( !vol_basis_init() ) {
    fprintf( stderr, "ERROR vol_basis_init() failed\n" );
    return 1;
  }

  printf( "Loading combined header&sequence file `%s`\n", filename_vols );

  vologram_t volograms[5];
  uint32_t start_frames[5] = { 0, 30, 60, 90, 120 };
  vec3 positions[5]        = {
    ( vec3 ){ 0.0f, 0.0f, 0.0f },  //
    ( vec3 ){ -1.0f, 0.0f, 0.0f }, //
    ( vec3 ){ 1.0f, 0.0f, 0.0f },  //
    ( vec3 ){ -2.0f, 0.0f, 0.0f }, //
    ( vec3 ){ 2.0f, 0.0f, 0.0f }   //
  };
  vec4 tints[5] = {
    ( vec4 ){ 1, 1, 1, 1 },     //
    ( vec4 ){ 1, 0, 0, 0 },     //
    ( vec4 ){ 0.1, 1, 0.1, 1 }, //
    ( vec4 ){ 0.1, 0.1, 1, 1 }, //
    ( vec4 ){ 1, 0, 1, 1 }      //
  };
  for ( int i = 0; i < n_volograms; i++ ) {
    volograms[i] = _create_vologram( filename_vols, positions[i], start_frames[i], &sounds[i], &engine[i] );
    if ( !volograms[i].loaded ) {
      fprintf( stderr, "ERROR creating vologram %i from file `%s`\n", i, filename_vols );
      return 1;
    }
  }

  printf( "rendering start...\n" );

  double prev_s          = gfx_get_time_s();
  double window_update_s = 0.0;
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

    window_update_s += elapsed_s;
    if ( window_update_s >= 0.5 ) {
      window_update_s -= 0.5;
      double gfx_fps = 1.0 / elapsed_s;
      char str[256];
      sprintf( str, "Demo %.0f", gfx_fps );
      gfx_window_set_title( str );
    }

    for ( int i = 0; i < n_volograms; i++ ) { _update_vologram( elapsed_s, &volograms[i] ); }

    mat4 P = perspective( 66.6f, aspect, 0.1f, 100.0f );
    mat4 V = look_at( ( vec3 ){ 0, 1, 2 }, ( vec3 ){ 0, 1, 0 }, ( vec3 ){ 0, 1, 0 } );

    gfx_clear_colour_and_depth_buffers( 0.5f, 0.5f, 0.5f, 1.0f );

    for ( int i = 0; i < n_volograms; i++ ) {
      gfx_uniform4fv( gfx_default_textured_shader, gfx_default_textured_shader.u_tint, 1, &tints[i].x );
      _draw_vologram( P, V, &volograms[i] );
    }

    gfx_swap_buffer();
  }

  gfx_stop();
  for ( int i = 0; i < n_volograms; i++ ) { ma_engine_uninit( &engine[i] ); }

  if ( output_blocks_ptr ) { free( output_blocks_ptr ); }

  printf( "Normal exit.\n" );
  return 0;
}
