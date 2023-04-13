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

  // validate file exists and is not a directory

  // validate file has a header in the top

  // validate file has a sequence following the header

  // TODO play the contents - new API in vol_geom required I guess.

  printf( "Normal exit.\n" );
  return 0;
}
