/* ===============================================================================================
URL:     https://github.com/capnramses/
Licence: See bottom of file.
Author:  Anton Gerdelan <antonofnote at gmail> @capnramses
==================================================================================================
*/

#ifndef GFX_H_
#define GFX_H_

#ifdef __cplusplus
extern "C" {
#endif /* CPP */

#include "apg_maths.h"
#include <stdbool.h>
#include <stddef.h> // size_t
#include <stdint.h>

typedef struct gfx_mesh_t {
  uint32_t vao;
  uint32_t points_vbo, texcoords_vbo, normals_vbo, indices_vbo;
  size_t n_vertices;
  size_t n_indices;
  uint8_t indices_type; // 0=ubyte, 1=ushort, 2=uint
  bool dynamic;
} gfx_mesh_t;

#define GFX_SHADER_PATH_MAX 1024
typedef struct gfx_shader_t {
  uint32_t program_gl;
  // mat4
  int u_P, u_V, u_M;
  // vec4
  int u_tint;
  // vec2
  int u_pos, u_scale, u_texcoord_scale;
  // float
  int u_time;
  // int
  int u_texture_a; // default to value 0
  int u_texture_b; // default to value 1

  char vs_filename[GFX_SHADER_PATH_MAX], fs_filename[GFX_SHADER_PATH_MAX];
  bool loaded;
} gfx_shader_t;

bool gfx_start( const char* window_title, int w, int h, bool fullscreen );
void gfx_stop();

void gfx_depth_testing( bool enable );
bool gfx_should_window_close();
void gfx_framebuffer_dims( int* width, int* height );
void gfx_window_dims( int* width, int* height );
void gfx_window_set_dims( int width, int height );
void gfx_window_set_title( const char* title_str );
void gfx_viewport( int x, int y, int w, int h );
void gfx_clear_colour_and_depth_buffers( float r, float g, float b, float a );
void gfx_swap_buffer();
void gfx_poll_events();
void gfx_backface_culling( bool enable );
void gfx_winding_cw( bool enable );

/*
PARAMS
 indices_buffer    - An optional ( can be NULL ) array of indices, one index per vertex. Type is either ubyte, ushort, or uint - specified in indices_type
 indices_buffer_sz - Size of the indices_buffer in bytes. The number of indices in the buffer will be inferred from this size / indices_type size (1,2, or 4).
 indices_type      - Data type of indices - 0=unsigned byte, 1=unsigned short, 2=unsigned int.
*/
gfx_mesh_t gfx_create_mesh_from_mem(                                          //
  const float* points_buffer, int n_points_comps,                             //
  const float* texcoords_buffer, int n_texcoord_comps,                        //
  const float* normals_buffer, int n_normal_comps,                            //
  const void* indices_buffer, size_t indices_buffer_sz, uint8_t indices_type, //
  int n_vertices, bool dynamic );

void gfx_update_mesh_from_mem( gfx_mesh_t* mesh,                              //
  const float* points_buffer, int n_points_comps,                             //
  const float* texcoords_buffer, int n_texcoord_comps,                        //
  const float* normals_buffer, int n_normal_comps,                            //
  const void* indices_buffer, size_t indices_buffer_sz, uint8_t indices_type, //
  int n_vertices, bool dynamic );

void gfx_delete_mesh( gfx_mesh_t* mesh );

gfx_shader_t gfx_create_shader_program_from_files( const char* vert_shader_filename, const char* frag_shader_filename );
gfx_shader_t gfx_create_shader_program_from_strings( const char* vert_shader_str, const char* frag_shader_str );
void gfx_delete_shader_program( gfx_shader_t* shader );

typedef struct gfx_texture_properties_t {
  bool is_srgb, is_bgr, is_depth, is_array, has_mips, repeats, bilinear;
} gfx_texture_properties_t;

typedef struct gfx_texture_t {
  uint32_t handle_gl;
  int w, h, n_channels;
  gfx_texture_properties_t properties;
} gfx_texture_t;

gfx_texture_t gfx_create_texture_from_mem( const uint8_t* img_buffer, int w, int h, int n_channels, gfx_texture_properties_t properties );
void gfx_delete_texture( gfx_texture_t* texture );
// PARAMS - if properties haven't changed you can use texture->is_depth etc.
void gfx_update_texture( gfx_texture_t* texture, const uint8_t* img_buffer, int w, int h, int n_channels );
void gfx_update_texture_sub_image( gfx_texture_t* texture, const uint8_t* img_buffer, int x_offs, int y_offs, int w, int h );

typedef enum gfx_primitive_type_t { GFX_PT_TRIANGLES = 0, GFX_PT_TRIANGLE_STRIP, GFX_PT_POINTS } gfx_primitive_type_t;

/* Draw a mesh, in a given primitive mode, with a shader, using virtual camera and local->world matrices, and optional array of textures */
void gfx_draw_mesh( gfx_mesh_t mesh, gfx_primitive_type_t pt, gfx_shader_t shader, float* P, float* V, float* M, gfx_texture_t* textures, int n_textures );
void gfx_draw_textured_quad( gfx_texture_t texture, vec2 scale, vec2 pos, vec2 texcoord_scale, vec4 tint_rgba );

void gfx_uniform1f( gfx_shader_t shader, int location, float f );

void gfx_wireframe_mode();
void gfx_polygon_mode();

double gfx_get_time_s();
bool input_is_key_held( int keycode );

#ifdef __cplusplus
}
#endif /* CPP */

#endif /* GFX_H_ */

/*
-------------------------------------------------------------------------------------
This software is available under two licences - you may use it under either licence.
-------------------------------------------------------------------------------------
FIRST LICENCE OPTION

>                                  Apache License
>                            Version 2.0, January 2004
>                         http://www.apache.org/licenses/
>    Copyright 2019 Anton Gerdelan.
>    Licensed under the Apache License, Version 2.0 (the "License");
>    you may not use this file except in compliance with the License.
>    You may obtain a copy of the License at
>        http://www.apache.org/licenses/LICENSE-2.0
>    Unless required by applicable law or agreed to in writing, software
>    distributed under the License is distributed on an "AS IS" BASIS,
>    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
>    See the License for the specific language governing permissions and
>    limitations under the License.
-------------------------------------------------------------------------------------
SECOND LICENCE OPTION

> This is free and unencumbered software released into the public domain.
>
> Anyone is free to copy, modify, publish, use, compile, sell, or
> distribute this software, either in source code form or as a compiled
> binary, for any purpose, commercial or non-commercial, and by any
> means.
>
> In jurisdictions that recognize copyright laws, the author or authors
> of this software dedicate any and all copyright interest in the
> software to the public domain. We make this dedication for the benefit
> of the public at large and to the detriment of our heirs and
> successors. We intend this dedication to be an overt act of
> relinquishment in perpetuity of all present and future rights to this
> software under copyright law.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
> EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
> MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
> IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
> OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
> ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
> OTHER DEALINGS IN THE SOFTWARE.
>
> For more information, please refer to <http://unlicense.org>
-------------------------------------------------------------------------------------
*/
