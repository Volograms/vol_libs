/** vol_av ffmpeg demo
 * Authors:   Anton Gerdelan <anton@volograms.com>
 * Copyright: 2021, Volograms (http://volograms.com/)
 * Language:  C99
 * Licence:   The MIT License. See LICENSE.md for details.
 * Notes:
 *
 * This is a demo of how to use our little ffmpeg wrapper, "vol_av" to pull frames of image data out of an MP4.
 * You can reuse the wrapper or look up each function to see how they interact with ffmpeg.
 *
 * RUN         - get_images MYVIDEO.mp4
 */

#include "vol_av.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

    /* write frame to a file here if you like or do stuff with it
     */
  } // endfor

  // close video file and reset struct memory
  ret = vol_av_close( &info_ptr );
  if ( !ret ) { fprintf( stderr, "WARNING: closing video file `%s`\n", filename ); }

  return 0;
}
