/** @file av_timed_test.c
 * Performance timers against vol_av functions to check how fast it is with e.g. different numbers of threads enabled.
 *           |
 * --------- | ----------
 * Authors   | Anton Gerdelan <anton@volograms.com>
 * Copyright | 2022, Volograms (http://volograms.com/)
 * Language  | C99
 * Licence   | The MIT License. See LICENSE.md for details.
 */


#include <assert.h>
#include <stdbool.h>
#include <stdint.h>/* types */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h> /* for backtraces and timers */
#else
#include <unistd.h>  /* linux-only? */
#endif
/* includes for timers */
#ifdef _WIN32
#include <profileapi.h>
#elif __APPLE__
#include <mach/mach_time.h>
#else
#include <sys/time.h>
#endif

#include "vol_av.h"

static vol_av_video_t av_info;

/*=================================================================================================
TIME IMPLEMENTATION
These functions are copy-pasted from Public Domain snippets available at
https://github.com/capnramses/apg/blob/master/apg/apg.h
=================================================================================================*/
static uint64_t _frequency = 1000000, _offset;

void apg_time_init( void ) {
#ifdef _WIN32
  _frequency = 1000; // QueryPerformanceCounter default
  QueryPerformanceFrequency( (LARGE_INTEGER*)&_frequency );
  QueryPerformanceCounter( (LARGE_INTEGER*)&_offset );
#elif __APPLE__
  mach_timebase_info_data_t info;
  mach_timebase_info( &info );
  _frequency       = ( info.denom * 1e9 ) / info.numer;
  _offset          = mach_absolute_time();
#else
  _frequency = 1000000000; // nanoseconds
  struct timespec ts;
  clock_gettime( CLOCK_MONOTONIC, &ts );
  _offset = (uint64_t)ts.tv_sec * (uint64_t)_frequency + (uint64_t)ts.tv_nsec;
#endif
}

double apg_time_s( void ) {
#ifdef _WIN32
  uint64_t counter = 0;
  QueryPerformanceCounter( (LARGE_INTEGER*)&counter );
  return (double)( counter - _offset ) / _frequency;
#elif __APPLE__
  uint64_t counter = mach_absolute_time();
  return (double)( counter - _offset ) / _frequency;
#else
  struct timespec ts;
  clock_gettime( CLOCK_MONOTONIC, &ts );
  uint64_t counter = (uint64_t)ts.tv_sec * (uint64_t)_frequency + (uint64_t)ts.tv_nsec;
  return (double)( counter - _offset ) / _frequency;
#endif
}

/* NOTE: for linux -D_POSIX_C_SOURCE=199309L must be defined for glibc to get nanosleep() */
void apg_sleep_ms( int ms ) {
#ifdef _WIN32
  Sleep( ms ); /* NOTE(Anton) may not need this since using gcc on Windows and usleep() works */
#elif _POSIX_C_SOURCE >= 199309L
  struct timespec ts;
  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = ( ms % 1000 ) * 1000000;
  nanosleep( &ts, NULL );
#else
  usleep( ms * 1000 );
#endif
}

int main( int argc, char** argv ) {
  const char* video_file = "../samples/counter.mp4";
  bool ret               = false;

  if ( argc > 1 ) { video_file = argv[1]; }

  fprintf( stdout, "\033[0;32m" ); // Set to green.
  printf( "\n-----------------------------------\n Starting vol_av tests  \n-----------------------------------\n" );
  fprintf( stdout, "\033[0m" ); // Reset to default colour.
  printf( "Using video file input `%s`\n", video_file );

 // vol_av_set_log_callback( NULL );

  memset( &av_info, 0, sizeof( vol_av_video_t ) );
  ret = vol_av_open( video_file, &av_info );
  if ( !ret ) { return false; }

  int w = 0, h = 0;
  vol_av_dimensions( &av_info, &w, &h );
  printf( "Dimensions = %ix%i\n", w, h );

  double fps = vol_av_frame_rate( &av_info );
  printf( "fps = %.2f\n", fps );

  int64_t n_frames = vol_av_frame_count( &av_info );
  printf( "n_frames = %i\n", (int)n_frames );

  double duration_s = vol_av_duration_s( &av_info );
  printf( "duration_s = %.2f\n", (float)duration_s );

  // start timing
  apg_time_init();
  double start_s = apg_time_s();

  ret = vol_av_read_next_frame( &av_info );
  if ( !ret ) { return false; }
  // read the next 1799 frames
  int n = n_frames < 1800 ? n_frames : 1800;
  for ( int i = 1; i < n; i++ ) {
    ret = vol_av_read_next_frame( &av_info );
    if ( !ret ) { return false; }
  }
  double end_s = apg_time_s();

  ret = vol_av_close( &av_info );
  if ( !ret ) { return false; }
  memset( &av_info, 0, sizeof( vol_av_video_t ) );

  double average_s = ( end_s - start_s ) / n;
  printf("Average read_next_frame time was %lfms\n", average_s * 1000.0 );

  fprintf( stdout, "\033[0;32m" ); // Set to green.
  printf( "\n-----------------------------------\n vol_av tests passed. Normal exit  \n-----------------------------------\n" );
  fprintf( stdout, "\033[0m" ); // Reset to default colour.

  printf( "normal exit\n" );
  return 0;
}
