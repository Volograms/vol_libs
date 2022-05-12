/** vol_av ffmpeg demo
 * Authors:   Anton Gerdelan <anton@volograms.com>
 * Copyright: 2021, Volograms (http://volograms.com/)
 * Language:  C99
 * Licence:   The MIT License. See LICENSE.md for details.
 * Notes:
 *
 * 21 May 2021
 *
 * This is a demo of how to use our little ffmpeg wrapper, "vol_av" to pull frames of image data out of an MP4.
 * You can reuse the wrapper or look up each function to see how they interact with ffmpeg.
 *
 * Included in here is a function to save every single image from the video as a PPM image file (open in eg GIMP).
 * This is just an example.
 * Beware this could use a lot of HD space for a big video!
 *
 * RUN         - dump_images MYVIDEO.mp4
 */

#include "vol_av.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/// example function to do something with data
static bool write_rgb_image_to_ppm( const char* filename, uint8_t* image_ptr, int w, int h ) {
  FILE* f_ptr = fopen( filename, "w" );
  if ( !f_ptr ) { return false; }
  fprintf( f_ptr, "P3\n%i %i\n255\n", w, h );
  for ( int y = 0; y < h; y++ ) {
    for ( int x = 0; x < w; x++ ) {
      int idx = ( y * w + x ) * 3;
      fprintf( f_ptr, "%i %i %i ", image_ptr[idx + 0], image_ptr[idx + 1], image_ptr[idx + 2] );
    }
    fprintf( f_ptr, "\n" );
  }
  fclose( f_ptr );
  return true;
}

int main( int argc, char** argv ) {
  vol_av_video_t info_ptr;
  memset( &info_ptr, 0, sizeof( vol_av_video_t ) ); // zero the struct paramters

  if ( argc < 2 ) {
    printf( "usage: %s MYVIDEO.MP4\n", argv[0] );
    return 0;
  }
  const char* filename = argv[1];
  printf( "Opening video `%s`\n", filename );

  // open the video file and put data about it in the provided struct
  bool ret = vol_av_open( filename, &info_ptr );
  if ( !ret ) {
    fprintf( stderr, "ERROR: opening video file `%s`\n", filename );
    return 1;
  }

  int64_t n_frames = 0;
  { // get some info about the video images
    int w = 0, h = 0;
    vol_av_dimensions( &info_ptr, &w, &h );
    printf( "Video has dimensions of %ix%i\n", w, h );

    double fps = vol_av_frame_rate( &info_ptr );
    printf( "Video framerate is %f FPS\n", (float)fps );

    n_frames = vol_av_frame_count( &info_ptr );
    printf( "There are %i frames in the video\n", (int)n_frames );

    double duration_s = vol_av_duration_s( &info_ptr );
    printf( "The video is %f seconds long\n", (float)duration_s );
  }

  // get RGB memory for each frame of the video, one by one
  for ( int64_t i = 0; i < n_frames; i++ ) {
    ret = vol_av_read_next_frame( &info_ptr );
    if ( !ret ) { fprintf( stderr, "ERROR: fetching video frame number %i/%i\n", (int)i, (int)n_frames ); }
    if ( !info_ptr.pixels_ptr ) { fprintf( stderr, "ERROR: pixels pointer NULL at  frame number %i/%i\n", (int)i, (int)n_frames ); }

    /* write frame to a file here if you like or do stuff with it
     */
    char output_fn[256];
    sprintf( output_fn, "frame_%08i.ppm", (int)i + 1 ); // note + 1 (so first frame filename is not 0).
    printf( "writing frame %i/%i (starting at 1) to file `%s`...\n", (int)i+1, (int)n_frames, output_fn );
    ret = write_rgb_image_to_ppm( output_fn, info_ptr.pixels_ptr, info_ptr.w, info_ptr.h );
    if ( !ret ) { fprintf( stderr, "ERROR: writing image frame %i to file `%s`\n", (int)i, output_fn ); }
  } // endfor

  // close video file and reset struct memory
  ret = vol_av_close( &info_ptr );
  if ( !ret ) { fprintf( stderr, "WARNING: closing video file `%s`\n", filename ); }

  return 0;
}
