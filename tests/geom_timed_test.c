/** @file geom_timed_test.c
 * Performance timers against vol_geom functions to check how much faster pre-loading the sequence file is,
 * rather than reading the file on each frame load - "streaming mode".
 *           |
 * --------- | ----------
 * Authors   | Anton Gerdelan <anton@volograms.com>
 * Copyright | 2021, Volograms (http://volograms.com/)
 * Language  | C99
 * Licence   | The MIT License. See LICENSE.md for details.
 */

#include "vol_geom.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>/* size_t */
#include <stdio.h> /* FILE* */
#include <time.h>
#ifdef _WIN32
#include <windows.h> /* for time */
#elif __APPLE__
#include <mach/mach_time.h>
#endif
#include <unistd.h>

static vol_geom_info_t geom_info;
static vol_geom_frame_data_t geom_frame_data;

/*=================================================================================================
TIME IMPLEMENTATION
These functions are copy-pasted from Public Domain snippets available at
https://github.com/capnramses/apg/blob/master/apg/apg.h
=================================================================================================*/
static uint64_t _frequency = 1000000, _offset;

void apg_time_init( void ) {
#ifdef _WIN32
  uint64_t counter;
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
#ifdef WIN32
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

static bool _process_vologram( const char* hdr, const char* seq, bool streaming ) {
  vol_geom_info_t geom_info;
  vol_geom_frame_data_t frame_data;
  double load_i_s = apg_time_s();
  if ( !vol_geom_create_file_info( hdr, seq, &geom_info, streaming ) ) {
    fprintf( stderr, "ERROR: loading vologram\n" );
    return false;
  }
  double load_f_s = apg_time_s();
  printf( "---vologram load time %lfs\n", load_f_s - load_i_s );
  //
  double play_i_s = apg_time_s();
  int n_frames    = geom_info.hdr.frame_count;
  for ( int i = 0; i < n_frames; i++ ) {
    if ( !vol_geom_read_frame( seq, &geom_info, i, &frame_data ) ) {
      fprintf( stderr, "ERROR: reading frame %i\n", i );
      return false;
    }
  }
  double play_f_s = apg_time_s();
  printf( "---vologram frame processing time %lfs\n", play_f_s - play_i_s );

  if ( !vol_geom_free_file_info( &geom_info ) ) {
    fprintf( stderr, "ERROR: freeing data\n" );
    return false;
  }

  return true;
}

int main( int argc, char** argv ) {
  char *hdr_filename = NULL, *seq_filename = NULL;
  apg_time_init();

  if ( argc >= 3 ) {
    hdr_filename = argv[1];
    seq_filename = argv[2];
  } else {
    printf( "Usage: %s HEADER.vols SEQUENCE.vols\n", argv[0] );
    return 0;
  }

  // It seems like non-streaming runs >=1.5x the speed during processing.
  printf( "Non-streaming:\n" );
  if ( !_process_vologram( hdr_filename, seq_filename, false ) ) { return 1; }

  printf( "Streaming:\n" );
  if ( !_process_vologram( hdr_filename, seq_filename, true ) ) { return 1; }

  printf( "normal exit\n" );
  return 0;
}
