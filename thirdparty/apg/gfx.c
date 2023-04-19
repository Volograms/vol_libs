/* ===============================================================================================
URL:     https://github.com/capnramses/
Licence: See bottom of file.
Author:  Anton Gerdelan <antonofnote at gmail> @capnramses
==================================================================================================
*/

#include "gfx.h"
#include "glcontext.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define GFX_SHADER_BINDING_VP 0
#define GFX_SHADER_BINDING_VT 1
#define GFX_SHADER_BINDING_VN 2
#define GFX_SHADER_BINDING_VC 3
#define GFX_MAX_SHADER_STR 10000

GLFWwindow* gfx_window_ptr; // extern in apg_glcontext.h
gfx_shader_t gfx_default_shader, gfx_quad_texture_shader;
gfx_mesh_t gfx_ss_quad_mesh;

static GLFWmonitor* gfx_monitor_ptr;
static int g_win_width = 1920, g_win_height = 1080;

static void _init_ss_quad( void ) {
  float ss_quad_pos[] = { -1.0, 1.0, -1.0, -1.0, 1.0, 1.0, 1.0, -1.0 };
  gfx_ss_quad_mesh    = gfx_create_mesh_from_mem( //
    ss_quad_pos, 2,                            // vp
    NULL, 0,                                   // vt
    NULL, 0,                                   // vn
    NULL, 0, 0,                                // indices & sz & type
    4,                                         // n verts
    false                                      // dynamic
  );
}

bool gfx_start( const char* window_title, int w, int h, bool fullscreen ) {
  g_win_width  = w;
  g_win_height = h;
  { // GLFW
    if ( !glfwInit() ) {
      fprintf( stderr, "ERROR: could not start GLFW3\n" );
      return false;
    }

    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4 );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 1 );
    glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE );
    glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
    { // MSAA hint
      // NOTE(Anton) fetching max samples didn't work on my linux nvidia driver before window creation, which kind of defeats the purpose of asking
      int nsamples = 16;
      glfwWindowHint( GLFW_SAMPLES, nsamples );
    }
    if ( fullscreen ) {
      gfx_monitor_ptr         = glfwGetPrimaryMonitor();
      const GLFWvidmode* mode = glfwGetVideoMode( gfx_monitor_ptr );
      glfwWindowHint( GLFW_RED_BITS, mode->redBits );
      glfwWindowHint( GLFW_GREEN_BITS, mode->greenBits );
      glfwWindowHint( GLFW_BLUE_BITS, mode->blueBits );
      glfwWindowHint( GLFW_REFRESH_RATE, mode->refreshRate );
      g_win_width  = mode->width;
      g_win_height = mode->height;
    }

    // NOTE(Anton) underlying window systems can create a small graphics driver memory leak here that isn't cleaned up on Terminate().
    // I found out by calling glfwTerminate() immediately before and after this function and this is the culprit (on Ubuntu at least).
    gfx_window_ptr = glfwCreateWindow( g_win_width, g_win_height, window_title, gfx_monitor_ptr, NULL );
    if ( !gfx_window_ptr ) {
      fprintf( stderr, "ERROR: could not open window with GLFW3\n" );
      glfwTerminate();
      return false;
    }

    glfwMakeContextCurrent( gfx_window_ptr );
  }

  { // GLAD
    if ( !gladLoadGL() ) {
      fprintf( stderr, "FATAL ERROR: Could not load OpenGL header using Glad.\n" );
      return false;
    }
    printf( "OpenGL %d.%d\n", GLVersion.major, GLVersion.minor );
  }

  const GLubyte* renderer = glGetString( GL_RENDERER );
  const GLubyte* version  = glGetString( GL_VERSION );
  printf( "Renderer: %s\n", renderer );
  printf( "OpenGL version supported %s\n", version );
  {
    const char* vertex_shader =
      "#version 410 core\n"
      "in vec2 a_vp;\n"
      "uniform vec2 u_scale, u_pos, u_texcoord_scale;\n"
      "out vec2 v_st;\n"
      "void main () {\n"
      "  v_st = a_vp.xy * 0.5 + 0.5;\n"
      "  v_st.t = 1.0 - v_st.t;\n"
      "  v_st *= u_texcoord_scale;\n"
      "  gl_Position = vec4( a_vp * u_scale + u_pos, 0.0, 1.0 );\n"
      "}\n";
    const char* fragment_shader =
      "#version 410 core\n"
      "in vec2 v_st;\n"
      "uniform sampler2D u_tex;\n"
      "uniform vec4 u_tint;\n"
      "out vec4 o_frag_colour;\n"
      "void main () {\n"
      "  vec4 texel = texture( u_tex, v_st );\n"
      "  o_frag_colour = texel * u_tint;\n"
      "  o_frag_colour.rgb = pow( o_frag_colour.rgb, vec3( 1.0 / 2.2 ) );\n"
      "}\n";
    gfx_quad_texture_shader = gfx_create_shader_program_from_strings( vertex_shader, fragment_shader );
    if ( !gfx_quad_texture_shader.loaded ) { return false; }
  }
  _init_ss_quad();
  glClearColor( 0.5, 0.5, 0.5, 1.0 );
  glDepthFunc( GL_LESS );
  glEnable( GL_DEPTH_TEST );
  gfx_backface_culling( true );

  glfwSwapInterval( 1 ); // vsync

  return true;
}

void gfx_stop( void ) {
  if ( gfx_quad_texture_shader.loaded ) { gfx_delete_shader_program( &gfx_quad_texture_shader ); }
  glfwTerminate();
}

void gfx_depth_testing( bool enable ) {
  if ( enable ) {
    glDepthFunc( GL_LESS );
    glEnable( GL_DEPTH_TEST );
  } else {
    glDisable( GL_DEPTH_TEST );
  }
}

bool gfx_should_window_close( void ) {
  assert( gfx_window_ptr );
  return glfwWindowShouldClose( gfx_window_ptr );
}

void gfx_viewport( int x, int y, int w, int h ) { glViewport( x, y, w, h ); }

void gfx_clear_colour_and_depth_buffers( float r, float g, float b, float a ) {
  glClearColor( r, g, b, a );
  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}

void gfx_framebuffer_dims( int* width, int* height ) {
  assert( width && height && gfx_window_ptr );
  glfwGetFramebufferSize( gfx_window_ptr, width, height );
}
void gfx_window_dims( int* width, int* height ) {
  assert( width && height && gfx_window_ptr );
  glfwGetWindowSize( gfx_window_ptr, width, height );
}

void gfx_window_set_dims( int width, int height ) {
  assert( gfx_window_ptr );
  glfwSetWindowSize( gfx_window_ptr, width, height );
}

void gfx_window_set_title( const char* title_str ) { glfwSetWindowTitle( gfx_window_ptr, title_str ); }

void gfx_swap_buffer( void ) {
  assert( gfx_window_ptr );

  glfwSwapBuffers( gfx_window_ptr );
}

void gfx_poll_events( void ) {
  assert( gfx_window_ptr );

  glfwPollEvents();
}

static GLenum _winding_dir = GL_CCW;

void gfx_backface_culling( bool enable ) {
  if ( enable ) {
    glCullFace( GL_BACK );
    glFrontFace( _winding_dir );
    glEnable( GL_CULL_FACE );
  } else {
    glDisable( GL_CULL_FACE );
  }
}

void gfx_winding_cw( bool enable ) {
  if ( enable ) {
    _winding_dir = GL_CW;
  } else {
    _winding_dir = GL_CCW;
  }
  glFrontFace( _winding_dir );
}

gfx_mesh_t gfx_create_mesh_from_mem(                                          //
  const float* points_buffer, int n_points_comps,                             //
  const float* texcoords_buffer, int n_texcoord_comps,                        //
  const float* normals_buffer, int n_normal_comps,                            //
  const void* indices_buffer, size_t indices_buffer_sz, uint8_t indices_type, //
  int n_vertices, bool dynamic ) {
  gfx_mesh_t mesh = ( gfx_mesh_t ){ .indices_type = indices_type, .n_vertices = n_vertices, .dynamic = dynamic };

  glGenVertexArrays( 1, &mesh.vao );
  glGenBuffers( 1, &mesh.points_vbo );
  glGenBuffers( 1, &mesh.texcoords_vbo );
  glGenBuffers( 1, &mesh.normals_vbo );
  glGenBuffers( 1, &mesh.indices_vbo );

  gfx_update_mesh_from_mem( &mesh, points_buffer, n_points_comps, texcoords_buffer, n_texcoord_comps, normals_buffer, n_normal_comps, indices_buffer,
    indices_buffer_sz, indices_type, n_vertices, dynamic );

  return mesh;
}

void gfx_update_mesh_from_mem( gfx_mesh_t* mesh,                              //
  const float* points_buffer, int n_points_comps,                             //
  const float* texcoords_buffer, int n_texcoord_comps,                        //
  const float* normals_buffer, int n_normal_comps,                            //
  const void* indices_buffer, size_t indices_buffer_sz, uint8_t indices_type, //
  int n_vertices, bool dynamic ) {
  // assert( points_buffer && n_points_comps > 0 );
  if ( !points_buffer || n_points_comps <= 0 ) { return; }
  assert( mesh && mesh->vao && mesh->points_vbo );
  if ( !mesh || !mesh->vao || !mesh->points_vbo ) { return; }

  GLenum usage     = !dynamic ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
  mesh->n_vertices = n_vertices;
  mesh->dynamic    = dynamic;

  glBindVertexArray( mesh->vao );
  if ( points_buffer && n_points_comps > 0 && mesh->points_vbo ) {
    glBindBuffer( GL_ARRAY_BUFFER, mesh->points_vbo );
    glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * n_points_comps * n_vertices, points_buffer, usage );
    glEnableVertexAttribArray( GFX_SHADER_BINDING_VP );
    glVertexAttribPointer( GFX_SHADER_BINDING_VP, n_points_comps, GL_FLOAT, GL_FALSE, 0, NULL );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
  }
  if ( texcoords_buffer && n_texcoord_comps > 0 && mesh->texcoords_vbo ) {
    glBindBuffer( GL_ARRAY_BUFFER, mesh->texcoords_vbo );
    glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * n_texcoord_comps * n_vertices, texcoords_buffer, usage );
    glEnableVertexAttribArray( GFX_SHADER_BINDING_VT );
    glVertexAttribPointer( GFX_SHADER_BINDING_VT, n_texcoord_comps, GL_FLOAT, GL_FALSE, 0, NULL );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
  }
  if ( normals_buffer && n_normal_comps > 0 && mesh->normals_vbo ) {
    glBindBuffer( GL_ARRAY_BUFFER, mesh->normals_vbo );
    glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * n_normal_comps * n_vertices, normals_buffer, usage );
    glEnableVertexAttribArray( GFX_SHADER_BINDING_VN );
    glVertexAttribPointer( GFX_SHADER_BINDING_VN, n_normal_comps, GL_FLOAT, GL_FALSE, 0, NULL );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
  }
  if ( indices_buffer && mesh->indices_vbo ) {
    mesh->indices_type = indices_type;
    mesh->n_indices    = 0;
    if ( indices_buffer_sz > 0 ) {
      switch ( indices_type ) {
      case 0: mesh->n_indices = indices_buffer_sz; break;     // ubyte
      case 1: mesh->n_indices = indices_buffer_sz / 2; break; // ushort
      case 2: mesh->n_indices = indices_buffer_sz / 4; break; // uint
      default: break;
      }
    }
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, mesh->indices_vbo );
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, indices_buffer_sz, indices_buffer, usage );
  }
  glBindVertexArray( 0 );
}

void gfx_delete_mesh( gfx_mesh_t* mesh ) {
  assert( mesh );

  if ( mesh->indices_vbo ) { glDeleteBuffers( 1, &mesh->indices_vbo ); }
  if ( mesh->normals_vbo ) { glDeleteBuffers( 1, &mesh->normals_vbo ); }
  if ( mesh->texcoords_vbo ) { glDeleteBuffers( 1, &mesh->texcoords_vbo ); }
  if ( mesh->points_vbo ) { glDeleteBuffers( 1, &mesh->points_vbo ); }
  if ( mesh->vao ) { glDeleteVertexArrays( 1, &mesh->vao ); }
  *mesh = ( gfx_mesh_t ){ .n_vertices = 0 };
}

gfx_shader_t gfx_create_shader_program_from_files( const char* vert_shader_filename, const char* frag_shader_filename ) {
  gfx_shader_t shader = ( gfx_shader_t ){ .program_gl = 0 };

  char vert_shader_str[GFX_MAX_SHADER_STR], frag_shader_str[GFX_MAX_SHADER_STR];
  {
    FILE* f_ptr = fopen( vert_shader_filename, "rb" );
    if ( !f_ptr ) {
      fprintf( stderr, "ERROR: opening shader file `%s`\n", vert_shader_filename );
      return shader;
    }
    size_t count = fread( vert_shader_str, 1, GFX_MAX_SHADER_STR - 1, f_ptr );
    assert( count < GFX_MAX_SHADER_STR - 1 ); // file was too long
    vert_shader_str[count] = '\0';
    fclose( f_ptr );
  }
  {
    FILE* f_ptr = fopen( frag_shader_filename, "rb" );
    if ( !f_ptr ) {
      fprintf( stderr, "ERROR: opening shader file `%s`\n", frag_shader_filename );
      return shader;
    }
    size_t count = fread( frag_shader_str, 1, GFX_MAX_SHADER_STR - 1, f_ptr );
    assert( count < GFX_MAX_SHADER_STR - 1 ); // file was too long
    frag_shader_str[count] = '\0';
    fclose( f_ptr );
  }

  shader = gfx_create_shader_program_from_strings( vert_shader_str, frag_shader_str );
  strncpy( shader.vs_filename, vert_shader_filename, GFX_SHADER_PATH_MAX - 1 );
  strncpy( shader.fs_filename, frag_shader_filename, GFX_SHADER_PATH_MAX - 1 );
  shader.vs_filename[GFX_SHADER_PATH_MAX - 1] = shader.fs_filename[GFX_SHADER_PATH_MAX - 1] = '\0';
  if ( !shader.program_gl ) { fprintf( stderr, "ERROR: failed to compile shader from files `%s` and `%s`\n", vert_shader_filename, frag_shader_filename ); }
  return shader;
}

static bool _recompile_shader_with_check( GLuint shader, const char* src_str ) {
  glShaderSource( shader, 1, &src_str, NULL );
  glCompileShader( shader );
  int params = -1;
  glGetShaderiv( shader, GL_COMPILE_STATUS, &params );
  if ( GL_TRUE != params ) {
    fprintf( stderr, "ERROR: shader index %u did not compile. src:\n%s\n", shader, src_str );
    int max_length    = 2048;
    int actual_length = 0;
    char slog[2048];
    glGetShaderInfoLog( shader, max_length, &actual_length, slog );
    fprintf( stderr, "shader info log for GL index %u:\n%s\n", shader, slog );
    if ( 0 != shader ) { glDeleteShader( shader ); }
    return false;
  }
  return true;
}

gfx_shader_t gfx_create_shader_program_from_strings( const char* vert_shader_str, const char* frag_shader_str ) {
  assert( vert_shader_str && frag_shader_str );

  gfx_shader_t shader = ( gfx_shader_t ){ .program_gl = 0 };
  GLuint vs           = glCreateShader( GL_VERTEX_SHADER );
  GLuint fs           = glCreateShader( GL_FRAGMENT_SHADER );
  bool res_a          = _recompile_shader_with_check( vs, vert_shader_str );
  bool res_b          = _recompile_shader_with_check( fs, frag_shader_str );
  shader.program_gl   = glCreateProgram();
  glAttachShader( shader.program_gl, fs );
  glAttachShader( shader.program_gl, vs );
  glBindAttribLocation( shader.program_gl, GFX_SHADER_BINDING_VP, "a_vp" );
  glBindAttribLocation( shader.program_gl, GFX_SHADER_BINDING_VT, "a_vt" );
  glBindAttribLocation( shader.program_gl, GFX_SHADER_BINDING_VN, "a_vn" );
  glBindAttribLocation( shader.program_gl, GFX_SHADER_BINDING_VC, "a_vc" );
  glLinkProgram( shader.program_gl );
  glDeleteShader( vs );
  glDeleteShader( fs );

  shader.u_M              = glGetUniformLocation( shader.program_gl, "u_M" );
  shader.u_V              = glGetUniformLocation( shader.program_gl, "u_V" );
  shader.u_P              = glGetUniformLocation( shader.program_gl, "u_P" );
  shader.u_texture_a      = glGetUniformLocation( shader.program_gl, "u_texture_a" );
  shader.u_texture_b      = glGetUniformLocation( shader.program_gl, "u_texture_b" );
  shader.u_pos            = glGetUniformLocation( shader.program_gl, "u_pos" );
  shader.u_scale          = glGetUniformLocation( shader.program_gl, "u_scale" );
  shader.u_texcoord_scale = glGetUniformLocation( shader.program_gl, "u_texcoord_scale" );
  shader.u_tint           = glGetUniformLocation( shader.program_gl, "u_tint" );
  shader.u_time           = glGetUniformLocation( shader.program_gl, "u_time" );

  if ( shader.u_texture_a > -1 ) { glProgramUniform1i( shader.program_gl, shader.u_texture_a, 0 ); }
  if ( shader.u_texture_b > -1 ) { glProgramUniform1i( shader.program_gl, shader.u_texture_b, 1 ); }

  bool linked = true;
  int params  = -1;
  glGetProgramiv( shader.program_gl, GL_LINK_STATUS, &params );
  if ( GL_TRUE != params ) {
    fprintf( stderr, "ERROR: could not link shader program GL index %u\n", shader.program_gl );
    int max_length    = 2048;
    int actual_length = 0;
    char plog[2048];
    glGetProgramInfoLog( shader.program_gl, max_length, &actual_length, plog );
    fprintf( stderr, "program info log for GL index %u:\n%s", shader.program_gl, plog );
    gfx_delete_shader_program( &shader );
    linked = false;
  }
  // fall back to default shader
  if ( !res_a || !res_b || !linked ) { return shader; } // should return default shader here

  shader.loaded = true;
  return shader;
}

void gfx_delete_shader_program( gfx_shader_t* shader ) {
  assert( shader );
  if ( shader->program_gl ) { glDeleteProgram( shader->program_gl ); }
  *shader = ( gfx_shader_t ){ .program_gl = 0 };
}

void gfx_update_texture_sub_image( gfx_texture_t* texture, const uint8_t* img_buffer, int x_offs, int y_offs, int w, int h ) {
  assert( texture && texture->handle_gl ); // NOTE: it is valid for pixels to be NULL

  GLenum format = GL_RGBA;
  glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
  switch ( texture->n_channels ) {
  case 4: {
    format = GL_RGBA;
  } break;
  case 3: {
    format = texture->properties.is_bgr ? GL_BGR : GL_RGB;
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 ); // for small 1-channel npot images and framebuffer reading
  } break;
  case 1: {
    format = GL_RED;
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 ); // for small 1-channel npot images and framebuffer reading
  } break;
  default: {
    fprintf( stderr, "WARNING: unhandled texture channel number: %i\n", texture->n_channels );
    gfx_delete_texture( texture );
    return;
    // TODO(Anton) - when have default texture: *texture = g_default_texture
    return;
  } break;
  } // endswitch
  // NOTE can use 32-bit, GL_FLOAT depth component for eg DOF
  if ( texture->properties.is_depth ) { format = GL_DEPTH_COMPONENT; }

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_2D, texture->handle_gl );
  glTexSubImage2D( GL_TEXTURE_2D, 0, x_offs, y_offs, w, h, format, GL_UNSIGNED_BYTE, img_buffer );
  glBindTexture( GL_TEXTURE_2D, 0 );
}

void gfx_update_texture( gfx_texture_t* texture, const uint8_t* img_buffer, int w, int h, int n_channels ) {
  assert( texture && texture->handle_gl );                         // NOTE: it is valid for pixels to be NULL
  assert( 4 == n_channels || 3 == n_channels || 1 == n_channels ); // 2 not used yet so not impl
  texture->w          = w;
  texture->h          = h;
  texture->n_channels = n_channels;

  GLint internal_format = GL_RGBA;
  GLenum format         = GL_RGBA;
  glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
  switch ( texture->n_channels ) {
  case 4: {
    internal_format = texture->properties.is_srgb ? GL_SRGB_ALPHA : GL_RGBA;
    format          = texture->properties.is_bgr ? GL_BGRA : GL_RGBA;
  } break;
  case 3: {
    internal_format = texture->properties.is_srgb ? GL_SRGB : GL_RGB;
    format          = texture->properties.is_bgr ? GL_BGR : GL_RGB;
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 ); // for small 1-channel npot images and framebuffer reading
  } break;
  case 1: {
    internal_format = GL_RED;
    format          = GL_RED;
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 ); // for small 1-channel npot images and framebuffer reading
  } break;
  default: {
    fprintf( stderr, "WARNING: unhandled texture channel number: %i\n", texture->n_channels );
    gfx_delete_texture( texture );
    // TODO(Anton) - when have default texture: *texture = g_default_texture
    return;
  } break;
  } // endswitch
  // NOTE can use 32-bit, GL_FLOAT depth component for eg DOF
  if ( texture->properties.is_depth ) {
    internal_format = GL_DEPTH_COMPONENT;
    format          = GL_DEPTH_COMPONENT;
  }

  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( GL_TEXTURE_2D, texture->handle_gl );
  glTexImage2D( GL_TEXTURE_2D, 0, internal_format, texture->w, texture->h, 0, format, GL_UNSIGNED_BYTE, img_buffer );
  if ( texture->properties.repeats ) {
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
  } else {
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
  }
  if ( texture->properties.bilinear ) {
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
  } else {
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  }
  if ( texture->properties.has_mips ) {
    glGenerateMipmap( GL_TEXTURE_2D );
    if ( texture->properties.bilinear ) {
      glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
    } else {
      glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR );
    }
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f );
  } else if ( texture->properties.bilinear ) {
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
  } else {
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
  }

  glBindTexture( GL_TEXTURE_2D, 0 );
}

gfx_texture_t gfx_create_texture_from_mem( const uint8_t* img_buffer, int w, int h, int n_channels, gfx_texture_properties_t properties ) {
  assert( 4 == n_channels || 3 == n_channels || 1 == n_channels ); // 2 not used yet so not impl
  gfx_texture_t texture = ( gfx_texture_t ){ .properties = properties };
  glGenTextures( 1, &texture.handle_gl );
  gfx_update_texture( &texture, img_buffer, w, h, n_channels );
  return texture;
}

void gfx_delete_texture( gfx_texture_t* texture ) {
  assert( texture && texture->handle_gl );
  glDeleteTextures( 1, &texture->handle_gl );
  memset( texture, 0, sizeof( gfx_texture_t ) );
}

void gfx_uniform1f( gfx_shader_t shader, int location, float f ) { glProgramUniform1f( shader.program_gl, location, f ); }

void gfx_draw_mesh( gfx_mesh_t mesh, gfx_primitive_type_t pt, gfx_shader_t shader, float* P, float* V, float* M, gfx_texture_t* textures, int n_textures ) {
  if ( 0 == mesh.points_vbo || 0 == mesh.n_vertices ) { return; }

  GLenum mode = GL_TRIANGLES;
  if ( pt == GFX_PT_TRIANGLE_STRIP ) { mode = GL_TRIANGLE_STRIP; }
  if ( pt == GFX_PT_POINTS ) { mode = GL_POINTS; }

  for ( int i = 0; i < n_textures; i++ ) {
    GLenum tex_type = !textures[i].properties.is_array ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY;
    glActiveTexture( GL_TEXTURE0 + i );
    glBindTexture( tex_type, textures[i].handle_gl );
  }

  glUseProgram( shader.program_gl );
  glProgramUniformMatrix4fv( shader.program_gl, shader.u_P, 1, GL_FALSE, P );
  glProgramUniformMatrix4fv( shader.program_gl, shader.u_V, 1, GL_FALSE, V );
  glProgramUniformMatrix4fv( shader.program_gl, shader.u_M, 1, GL_FALSE, M );
  glBindVertexArray( mesh.vao );
  if ( mesh.indices_vbo ) {
    GLenum type = GL_UNSIGNED_BYTE;
    switch ( mesh.indices_type ) {
    case 0: type = GL_UNSIGNED_BYTE; break;
    case 1: type = GL_UNSIGNED_SHORT; break;
    case 2: type = GL_UNSIGNED_INT; break;
    default: break;
    }
    glDrawElements( mode, (GLsizei)mesh.n_indices, type, NULL );
  } else {
    glDrawArrays( mode, 0, (GLsizei)mesh.n_vertices );
  }
  glBindVertexArray( 0 );
  glUseProgram( 0 );
  for ( int i = 0; i < n_textures; i++ ) {
    GLenum tex_type = !textures[i].properties.is_array ? GL_TEXTURE_2D : GL_TEXTURE_2D_ARRAY;
    glActiveTexture( GL_TEXTURE0 + i );
    glBindTexture( tex_type, 0 );
  }
}

void gfx_draw_textured_quad( gfx_texture_t texture, vec2 scale, vec2 pos, vec2 texcoord_scale, vec4 tint_rgba ) {
  GLenum tex_type = texture.properties.is_array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
  glEnable( GL_BLEND );
  glUseProgram( gfx_quad_texture_shader.program_gl );
  glProgramUniform2f( gfx_quad_texture_shader.program_gl, gfx_quad_texture_shader.u_scale, scale.x, scale.y );
  glProgramUniform2f( gfx_quad_texture_shader.program_gl, gfx_quad_texture_shader.u_pos, pos.x, pos.y );
  glProgramUniform2f( gfx_quad_texture_shader.program_gl, gfx_quad_texture_shader.u_texcoord_scale, texcoord_scale.x, texcoord_scale.y );
  glProgramUniform4f( gfx_quad_texture_shader.program_gl, gfx_quad_texture_shader.u_tint, tint_rgba.x, tint_rgba.y, tint_rgba.z, tint_rgba.w );
  glBindVertexArray( gfx_ss_quad_mesh.vao );
  glActiveTexture( GL_TEXTURE0 );
  glBindTexture( tex_type, texture.handle_gl );

  glDrawArrays( GL_TRIANGLE_STRIP, 0, (GLsizei)gfx_ss_quad_mesh.n_vertices );

  glBindTexture( tex_type, 0 );
  glBindVertexArray( 0 );
  glUseProgram( 0 );
  glDisable( GL_BLEND );
}

void gfx_wireframe_mode( void ) { glPolygonMode( GL_FRONT_AND_BACK, GL_LINE ); }

void gfx_polygon_mode( void ) { glPolygonMode( GL_FRONT_AND_BACK, GL_FILL ); }

double gfx_get_time_s( void ) { return glfwGetTime(); }

bool input_is_key_held( int keycode ) {
  assert( gfx_window_ptr && keycode >= 32 && keycode <= GLFW_KEY_LAST );
  return glfwGetKey( gfx_window_ptr, keycode );
}

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
