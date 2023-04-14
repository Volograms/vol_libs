/**
 *
 * Compile:
 *   gcc -g main.c ../../src/vol_geom.c -I ../../src/
 */

#include "vol_geom.h"
#include <stdio.h>

int main( int argc, char** argv ) {
  if ( argc < 2 ) {
    printf( "Usage: %s MYFILE.vols\n", argv[0] );
    return 0;
  }
  const char* filename_vols = argv[1];
  printf( "Loading combined header&sequence file `%s`\n", filename_vols );

  vol_geom_file_hdr_t hdr = ( vol_geom_file_hdr_t ){ .compression = 0 };
  vol_geom_size_t hdr_sz  = 0;
  if ( !vol_geom_read_hdr_from_file( filename_vols, &hdr, &hdr_sz ) ) {
    fprintf( stderr, "ERROR reading header from file\n" );
    return 1;
  }

  printf( "hdr_sz = %li\n", hdr_sz );

  vol_geom_info_t vols_info = ( vol_geom_info_t ){ .biggest_frame_blob_sz = 0 };
  if ( !vol_geom_create_file_info_from_file( filename_vols, &vols_info ) ) {
    fprintf( stderr, "ERROR vol_geom_create_file_info_from_file.\n" );
    return 1;
  }

  // TODO play the contents - see if it works with my cat file and an official combined v1.2 file.
  // TODO also make sure I didn't break the old split file.
  // TODO think longer term about deprecating 'file info' struct and just doing:
  //    vol_geom_read_hdr_from_file()
  //    vol_geom_read_frame_from_mem() for streaming.

  printf( "Normal exit.\n" );
  return 0;
}
