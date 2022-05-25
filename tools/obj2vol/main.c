// Basic tool to create a .vol pair from a very simple triangulated .obj with the correct vertex attributes.
// Very little validation is done, in other words.
// COMPILE: `gcc obj2vol.c`
// RUN:     `./a.out ../samples/cube.obj` (or any .obj you like) --> outputs `header.vol` and `sequence.vol`

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float *verts_ptr, *norms_ptr, *uvs_ptr;
static short* indices_short_ptr; // NOTE use Integer[] if # vertices >= 65535
static int verts_sz, norms_sz, indices_sz, uvs_sz, mesh_data_sz;
static int n_verts;

static bool _load_obj( const char* filename ) {
  if ( !filename ) { return false; }

  char line_str[2048];
  FILE* f_ptr = fopen( filename, "r" );
  if ( !f_ptr ) { return false; }

  // pass 1: count vertices and assume each face is a triangle
  int n_f = 0, n_v = 0, n_vn = 0, n_vt = 0;
  while ( fgets( line_str, 2048, f_ptr ) ) {
    if ( line_str[0] == 'f' ) {
      n_f++;
    } else if ( line_str[0] == 'v' ) {
      if ( line_str[1] == ' ' ) {
        n_v++;
      } else if ( line_str[1] == 'n' ) {
        n_vn++;
      } else if ( line_str[1] == 't' ) {
        n_vt++;
      }
    }
    line_str[0] = '\0';
  }
  if ( n_v < 3 ) {
    fprintf( stderr, "[obj2vol] ERROR: Expected at least 3 vertices in input OBJ file, but number of vertices found in file was %i.\n", n_v );
    return false;
  }
  if ( n_f < 1 ) {
    fprintf( stderr, "[obj2vol] ERROR: Expected at least 1 face in input OBJ file, but number of faces found in file was %i.\n", n_f );
    return false;
  }

  float* v_buff  = malloc( n_v * sizeof( float ) * 3 );
  float* vn_buff = malloc( n_vn * sizeof( float ) * 3 );
  float* vt_buff = malloc( n_vt * sizeof( float ) * 2 );
  int* f_buff    = malloc( n_f * sizeof( int ) * 9 );
  if ( !v_buff || !vn_buff || !vt_buff || !f_buff ) { return false; }

  // pass 2: parse values into buffers
  if ( 0 != fseek( f_ptr, 0, SEEK_SET ) ) { return false; }

  int f_idx = 0, v_idx = 0, vn_idx = 0, vt_idx = 0;
  while ( fgets( line_str, 2048, f_ptr ) ) {
    char token[256];
    token[0] = '\0';
    sscanf( line_str, "%s", token );
    if ( line_str[0] == 'f' ) {
      // f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3 ...
      int n = sscanf( line_str, "f %i/%i/%i %i/%i/%i %i/%i/%i", &f_buff[f_idx + 0], &f_buff[f_idx + 1], &f_buff[f_idx + 2], &f_buff[f_idx + 3],
        &f_buff[f_idx + 4], &f_buff[f_idx + 5], &f_buff[f_idx + 6], &f_buff[f_idx + 7], &f_buff[f_idx + 8] );
      assert( n == 9 );
      f_idx += n;
    } else if ( line_str[0] == 'v' ) {
      if ( line_str[1] == ' ' ) {
        int n = sscanf( line_str, "v %f %f %f", &v_buff[v_idx + 0], &v_buff[v_idx + 1], &v_buff[v_idx + 2] );
        assert( n == 3 );
        v_idx += n;
      } else if ( line_str[1] == 'n' ) {
        int n = sscanf( line_str, "vn %f %f %f", &vn_buff[vn_idx + 0], &vn_buff[vn_idx + 1], &vn_buff[vn_idx + 2] );
        assert( n == 3 );
        vn_idx += n;
      } else if ( line_str[1] == 't' ) {
        int n = sscanf( line_str, "vt %f %f", &vt_buff[vt_idx + 0], &vt_buff[vt_idx + 1] );
        assert( n == 2 );
        vt_idx += n;
      }
    } else if ( line_str[0] == 'o' ) {
      // object
    } else if ( line_str[0] == 'g' ) {
      // group
    } else if ( line_str[0] == 's' ) {
      // smoothing group
    } else if ( line_str[0] == '#' ) {
      // comment
    } else if ( strncmp( token, "mtllib", 6 ) == 0 ) {
      // material file
    } else if ( strncmp( token, "usemtl", 6 ) == 0 ) {
      // material name
    } else {
      fprintf( stderr, "WARNING -unhandled line `%s`\n", line_str );
    }
    line_str[0] = '\0';
  }
  printf( "f_idx = %i, n_f = %i\n", f_idx, n_f );
  assert( f_idx == n_f * 9 && n_f > 0 );

  n_verts = n_f * 3;
  if ( n_verts >= 65535 ) {
    fprintf( stderr, "WARNING - this vologram has %i vertices and .: should use int32 indices, but these are not implemented here yet!\n", n_verts );
    return false;
  }

  verts_sz     = n_verts * 3 * sizeof( float );
  norms_sz     = n_verts * 3 * sizeof( float );
  indices_sz   = n_verts * sizeof( short );
  uvs_sz       = n_verts * 2 * sizeof( float );
  mesh_data_sz = verts_sz + norms_sz + indices_sz + uvs_sz + sizeof( int32_t ) * 4;
  printf( "mesh_data_sz = %i\n", mesh_data_sz );
  verts_ptr         = malloc( verts_sz );
  norms_ptr         = malloc( norms_sz );
  indices_short_ptr = malloc( n_verts * sizeof( short ) );
  uvs_ptr           = malloc( uvs_sz );
  if ( !verts_ptr || !norms_ptr || !uvs_ptr || !indices_short_ptr ) { return false; }

  f_idx = v_idx = vt_idx = vn_idx = 0;
  printf( "f_idx 0-8 are: %i/%i/%i %i/%i/%i %i/%i/%i\n", f_buff[0], f_buff[1], f_buff[2], f_buff[3], f_buff[4], f_buff[5], f_buff[6], f_buff[7], f_buff[8] );
  for ( int i = 0; i < n_verts; i++ ) {
    // each vert: f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3 ...
    int v_idx  = f_buff[f_idx++];
    int vt_idx = f_buff[f_idx++];
    int vn_idx = f_buff[f_idx++];
    if ( i == 0 ) { printf( "v_idx = %i\nvt_idx = %i\nvn_idx = %i\n", v_idx, vt_idx, vn_idx ); }
    if ( i == 1 ) { printf( "v_idx = %i\nvt_idx = %i\nvn_idx = %i\n", v_idx, vt_idx, vn_idx ); }

    // v 1.000000 1.000000 -1.000000
    verts_ptr[i * 3 + 0] = v_buff[( v_idx - 1 ) * 3 + 0];
    verts_ptr[i * 3 + 1] = v_buff[( v_idx - 1 ) * 3 + 1];
    verts_ptr[i * 3 + 2] = v_buff[( v_idx - 1 ) * 3 + 2];
    uvs_ptr[i * 2 + 0]   = vt_buff[( vt_idx - 1 ) * 2 + 0];
    uvs_ptr[i * 2 + 1]   = vt_buff[( vt_idx - 1 ) * 2 + 1];
    norms_ptr[i * 3 + 0] = vn_buff[( vn_idx - 1 ) * 3 + 0];
    norms_ptr[i * 3 + 1] = vn_buff[( vn_idx - 1 ) * 3 + 1];
    norms_ptr[i * 3 + 2] = vn_buff[( vn_idx - 1 ) * 3 + 2];
    indices_short_ptr[i] = (short)i;
  }
  printf( "first ~16 indices:\n" );
  int first_indices = n_verts >= 16 ? 16 : n_verts;
  for ( int i = 0; i < first_indices; i++ ) { printf( "index %i) %i\n", i, (int)indices_short_ptr[i] ); }
  fclose( f_ptr );
  free( vt_buff );
  free( vn_buff );
  free( v_buff );
  free( f_buff );

  return true;
}

static bool _write_vols() {
  int frame_count = 1;
  { // write header.vol
    FILE* f_ptr = fopen( "header.vol", "wb" );
    if ( !f_ptr ) { return false; }

    uint8_t mn_sz           = 4;
    char magic_number[4]    = { 'V', 'O', 'L', 'S' };
    int version             = 12;
    int compression         = 0;
    uint8_t mesh_name_sz    = 0;
    uint8_t materialname_sz = 0;
    uint8_t shadername_sz   = 0;
    // UNUSED AT THE MOMENT -- char meshname[128]      = { 0 };
    // UNUSED AT THE MOMENT -- char materialname[128]  = { 0 };
    // UNUSED AT THE MOMENT -- char shadername[128]    = { 0 };
    int topology           = 0; // 0 means triangles
    bool normals           = true;
    bool textured          = false; // implies texture is stored inside keyframe
    unsigned short tex_w   = 0;
    unsigned short tex_h   = 0;
    unsigned short tex_fmt = 0; // ??? i guess '2' is RGB24
    float translation[3]   = { 0.0f };
    float rotation[4]      = { 1.0f, 0.0f, 0.0f, 0.0f };
    float scale            = 1.0f;

    int n = 0;
    n     = fwrite( &mn_sz, 1, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( magic_number, 1, 4, f_ptr );
    assert( 4 == n );
    n = fwrite( &version, 4, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &compression, 4, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &mesh_name_sz, 1, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &materialname_sz, 1, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &shadername_sz, 1, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &topology, 4, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &frame_count, 4, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &normals, 1, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &textured, 1, 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &tex_w, sizeof( unsigned short ), 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &tex_h, sizeof( unsigned short ), 1, f_ptr );
    assert( 1 == n );
    n = fwrite( &tex_fmt, sizeof( unsigned short ), 1, f_ptr );
    assert( 1 == n );
    n = fwrite( translation, sizeof( float ), 3, f_ptr );
    assert( 3 == n );
    n = fwrite( rotation, sizeof( float ), 4, f_ptr );
    assert( 4 == n );
    n = fwrite( &scale, sizeof( float ), 1, f_ptr );
    assert( 1 == n );

    fclose( f_ptr );
  }
  { // write sequence.vol
    FILE* f_ptr = fopen( "sequence.vol", "wb" );
    if ( !f_ptr ) { return false; }

    for ( int frame_number = 0; frame_number < frame_count; frame_number++ ) {
      uint8_t keyframe = 1;

      // frame hdr ( 9 bytes )
      int n = fwrite( &frame_number, sizeof( int32_t ), 1, f_ptr );
      assert( 1 == n );
      n = fwrite( &mesh_data_sz, sizeof( int32_t ), 1, f_ptr );
      assert( 1 == n );
      n = fwrite( &keyframe, sizeof( uint8_t ), 1, f_ptr );
      assert( 1 == n );

      printf( "verts_sz %i\n", verts_sz );
      printf( "norms_sz %i\n", norms_sz );
      printf( "indices_sz %i\n", indices_sz );
      printf( "uvs_sz %i\n", uvs_sz );

      assert( n_verts == ( verts_sz / (int)( sizeof( float ) * 3 ) ) );
      printf( "writing %i verts\n", n_verts );
      printf( "first 3 points components are { %.2f %.2f %.2f }\n", verts_ptr[0], verts_ptr[1], verts_ptr[2] );
      printf( "first 2 UV components are { %.2f %.2f }\n", uvs_ptr[0], uvs_ptr[1] );
      printf( "first 3 normals components are  { %.2f %.2f %.2f }\n", norms_ptr[0], norms_ptr[1], norms_ptr[2] );
      printf( "first 3 indices are  { %i, %i, %i }\n", (int)indices_short_ptr[0], (int)indices_short_ptr[1], (int)indices_short_ptr[2] );

      // frame bdy
      n = fwrite( &verts_sz, sizeof( int32_t ), 1, f_ptr );
      assert( 1 == n );
      n = fwrite( verts_ptr, 1, verts_sz, f_ptr );
      assert( verts_sz == n );
      n = fwrite( &norms_sz, sizeof( int32_t ), 1, f_ptr );
      assert( 1 == n );
      n = fwrite( norms_ptr, 1, norms_sz, f_ptr );
      assert( norms_sz == n );
      if ( 1 == keyframe ) { // keyframes only:
        n = fwrite( &indices_sz, sizeof( int32_t ), 1, f_ptr );
        assert( 1 == n );
        n = fwrite( indices_short_ptr, 1, indices_sz, f_ptr );
        assert( indices_sz == n );
        n = fwrite( &uvs_sz, sizeof( int32_t ), 1, f_ptr );
        assert( 1 == n );
        n = fwrite( uvs_ptr, 1, uvs_sz, f_ptr );
        assert( uvs_sz == n );
      }
      n = fwrite( &mesh_data_sz, sizeof( int32_t ), 1, f_ptr );
      assert( 1 == n );
    } // endfor

    fclose( f_ptr );
  }
  return true;
}

int main( int argc, char** argv ) {
  if ( argc < 2 ) {
    printf( "Usage: %s MESH.obj\n", argv[0] );
    return 0;
  }
  const char* obj_path = argv[1];
  printf( "Opening OBJ mesh `%s`\n", obj_path );
  bool ret = _load_obj( obj_path );
  if ( !ret ) {
    fprintf( stderr, "ERROR opening obj for reading\n" );
    return 1;
  }
  ret = _write_vols();
  if ( !ret ) {
    fprintf( stderr, "ERROR opening vol for writing\n" );
    return 1;
  }
  free( verts_ptr );
  free( norms_ptr );
  free( indices_short_ptr );
  free( uvs_ptr );

  printf( "normal exit\n" );
  return 0;
}
