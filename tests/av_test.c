/** @file av_test.c
 * Unit tests for vol_av using the counter.mp4
 * Tested in this program: the basic API against a simple mp4
 *
 *           |
 * --------- | ----------
 * Authors   | Anton Gerdelan <anton@volograms.com>
 * Copyright | 2021, Volograms (http://volograms.com/)
 * Language  | C99
 * Licence   | The MIT License. See LICENSE.md for details.
 */

#include "vol_av.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vol_av_video_t av_info;

static void _my_logger( vol_av_log_type_t log_type, const char* message_str ) {
  switch ( log_type ) { // Prepend a tag to the logs so we can tell that it's working.
  case VOL_AV_LOG_TYPE_INFO: {
    fprintf( stdout, "\033[0;36m" ); // Set to cyan.
    fprintf( stdout, "[vol_av info] %s", message_str );
  } break;
  case VOL_AV_LOG_TYPE_DEBUG: {
    fprintf( stdout, "\033[0;33m" ); // Set to purple.
    fprintf( stdout, "[vol_av debug] %s", message_str );
  } break;
  case VOL_AV_LOG_TYPE_WARNING: {
    fprintf( stderr, "\033[0;33m" ); // Set to yellow.
    fprintf( stderr, "[vol_av warning] %s", message_str );
  } break;
  case VOL_AV_LOG_TYPE_ERROR: {
    fprintf( stderr, "\033[0;31m" ); // Set to red.
    fprintf( stderr, "[vol_av error] %s", message_str );
  } break;
  default: break;
  }
  fprintf( stdout, "\033[0m" ); // Reset to default colour.
  fprintf( stderr, "\033[0m" ); // Reset to default colour.
}

// simple custom assert() to exit with an error code and the file:line that called it whenever a unit test fails.
#define VOL_ASSERT( x ) _vol_assert( x, __FILE__, __LINE__ );
static void _vol_assert( bool condition, const char* filename, int line ) {
  if ( !condition ) {
    fprintf( stderr, "\033[0;31m" ); // Set to red.
    fprintf( stderr, "TEST FAILED: %s:%i\n", filename, line );
    fprintf( stderr, "\033[0m" ); // Reset to default colour.
    vol_av_close( &av_info );
    exit( 1 );
  }
}

int main( int argc, char** argv ) {
  const char* video_file   = "../samples/counter.mp4";
  bool using_default_video = true;
  bool ret                 = false;

  if ( argc > 1 ) {
    video_file          = argv[1];
    using_default_video = false;
  }

  fprintf( stdout, "\033[0;32m" ); // Set to green.
  printf( "\n-----------------------------------\n\033[0;33m Starting vol_av tests \033[0m \n-----------------------------------\n" );
  fprintf( stdout, "\033[0m" ); // Reset to default colour.
  printf( "Using video file input `%s`\n", video_file );

  // start custom logger to see if it works
  vol_av_set_log_callback( _my_logger );

  // test failure to close
  ret = vol_av_close( &av_info );
  VOL_ASSERT( ret == false );

  // test loading the wrong file fails
  fprintf( stderr, "Test loading a non-existent file. This should fail and report an error:\n" );
  ret = vol_av_open( "WRONGFILE", &av_info );
  VOL_ASSERT( ret == false );
  vol_av_close( &av_info ); // free any residual allocations

  fprintf( stderr, "Test loading an existing file. This should pass and not report an error:\n" );
  memset( &av_info, 0, sizeof( vol_av_video_t ) );
  ret = vol_av_open( video_file, &av_info );
  VOL_ASSERT( ret );

  int w = 0, h = 0;
  vol_av_dimensions( &av_info, &w, &h );
  printf( "Dimensions = %ix%i\n", w, h );
  if ( using_default_video ) { VOL_ASSERT( w == 32 && h == 32 ); }

  double fps = vol_av_frame_rate( &av_info );
  printf( "fps = %.2f\n", fps );
  if ( using_default_video ) { VOL_ASSERT( w == 32 && h == 32 ); }

  int64_t n_frames = vol_av_frame_count( &av_info );
  printf( "n_frames = %i\n", (int)n_frames );
  if ( using_default_video ) { VOL_ASSERT( n_frames == 1800 ); }

  double duration_s = vol_av_duration_s( &av_info );
  printf( "duration_s = %.2f\n", (float)duration_s );
  if ( using_default_video ) { VOL_ASSERT( duration_s > 59.0 && duration_s < 61.0 ); }

  // read frame 0
  ret = vol_av_read_next_frame( &av_info );
  VOL_ASSERT( ret );
  VOL_ASSERT( av_info.pixels_ptr != NULL );
  printf( "Frame dimensions = %ix%i\n", av_info.w, av_info.h );
  if ( using_default_video ) { VOL_ASSERT( av_info.w == 32 && av_info.h == 32 ); }

  // read the next 1799 frames
  int n = n_frames < 1800 ? n_frames : 1800;
  for ( int i = 1; i < n; i++ ) {
    ret = vol_av_read_next_frame( &av_info );
    VOL_ASSERT( ret );
    VOL_ASSERT( av_info.pixels_ptr != NULL );
    if ( using_default_video ) { VOL_ASSERT( av_info.w == 32 && av_info.h == 32 ); }
  }

  // read a frame off the end ( make sure doesn't go into infinite loop here )

  ret = vol_av_read_next_frame( &av_info );
  VOL_ASSERT( ret );
  VOL_ASSERT( av_info.pixels_ptr != NULL );
  if ( using_default_video ) { VOL_ASSERT( av_info.w == 32 && av_info.h == 32 ); }
  ret = vol_av_read_next_frame( &av_info );
  VOL_ASSERT( ret );
  VOL_ASSERT( av_info.pixels_ptr != NULL );
  if ( using_default_video ) { VOL_ASSERT( av_info.w == 32 && av_info.h == 32 ); }

  ret = vol_av_close( &av_info );
  memset( &av_info, 0, sizeof( vol_av_video_t ) );
  VOL_ASSERT( ret );

  fprintf( stdout, "\033[0;32m" ); // Set to green.
  printf( "\n-----------------------------------\n\033[0;32m vol_av tests passed. Normal exit \033[0m \n-----------------------------------\n" );
  fprintf( stdout, "\033[0m" ); // Reset to default colour.
  return 0;
}
