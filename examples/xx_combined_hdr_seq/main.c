/**
 *
 * Compile:
 *   gcc main.c ../../src/vol_geom.c -I ../../src/
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

  // TODO validate - new API in vol_geom required I guess.
  //               - perhaps use a generic data interface (hdr_ptr seq_ptr)
  //               - with a get_seq_ptr() function we can use to point to data following the header
  //               - need to think ahead a bit about streaming - maybe just a new process_frame(next_frame_ptr) later.

  // validate file exists and is not a directory

  // validate file has a header in the top

  // validate file has a sequence following the header

  // TODO play the contents - new API in vol_geom required I guess.

  printf( "Normal exit.\n" );
  return 0;
}