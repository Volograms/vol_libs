/** @file vol_geom.c
 * Volograms Geometry Decoding API
 *
 * vol_geom  | .vol Geometry Decoding API
 * --------- | ---------------------
 * Version   | 0.13.0
 * Authors   | See matching header file.
 * Copyright | 2021, Volograms (http://volograms.com/)
 * Language  | C99
 * Files     | 2
 * Licence   | The MIT License. See LICENSE.md for details.
 */

#include "vol_geom.h"
#include <assert.h>
#include <inttypes.h> // 64-bit printfs (PRId64 for integer, PRIu64 for unsigned int, PRIx64 for hex)
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h> // Used for reading file sizes.
#include <sys/types.h>

// NOTE: ftello() and fseeko() are replace ftell(), fseek(), and their Windows equivalents, to support 64-bit indices to >2GB files.
#if defined( _WIN32 ) || defined( _WIN64 )
#define vol_geom_stat64 _stat64
#define vol_geom_stat64_t __stat64
#define vol_geom_fseeko _fseeki64
#define vol_geom_ftello _ftelli64
#else
#define vol_geom_stat64 stat
#define vol_geom_stat64_t stat
#define vol_geom_fseeko fseeko
#define vol_geom_ftello ftello
#endif

#define VOL_GEOM_LOG_STR_MAX_LEN 512 // Careful - this is stored on the stack to be thread and memory-safe so don't make it too large.
/// File header section size in bytes. Used in sanity checks to test for corrupted files that are below minimum sizes expected.
#define VOL_GEOM_FILE_HDR_V10_MIN_SZ 24 /// "VOLS" (4 bytes) + 4 string length bytes + 4 ints in v10 hdr.
/// File header section size in bytes. Used in sanity checks to test for corrupted files that are below minimum sizes expected.
#define VOL_GEOM_FRAME_MIN_SZ 17 /// 3 ints, 1 byte, 1 int inside vertices array. the rest are optional

/// Helper struct to refer to an entire file loaded from disk via `_read_file()`.
typedef struct vol_geom_file_record_t {
  uint8_t* byte_ptr;  // Pointer to contents of file.
  vol_geom_size_t sz; // Size of file in bytes.
} vol_geom_file_record_t;

static void _default_logger( vol_geom_log_type_t log_type, const char* message_str ) {
  FILE* stream_ptr = ( VOL_GEOM_LOG_TYPE_ERROR == log_type || VOL_GEOM_LOG_TYPE_WARNING == log_type ) ? stderr : stdout;
  fprintf( stream_ptr, "%s", message_str );
}

static void ( *_logger_ptr )( vol_geom_log_type_t log_type, const char* message_str ) = _default_logger;

// This function is used in this file as a printf-style logger. It converts that format to a simple string and passes it to _logger_ptr.
static void _vol_loggerf( vol_geom_log_type_t log_type, const char* message_str, ... ) {
  if ( !_logger_ptr ) { return; }
  char log_str[VOL_GEOM_LOG_STR_MAX_LEN];
  log_str[0] = '\0';
  va_list arg_ptr; // using va_args lets us make sure any printf-style formatting values are properly written into the string.
  va_start( arg_ptr, message_str );
  vsnprintf( log_str, VOL_GEOM_LOG_STR_MAX_LEN - 1, message_str, arg_ptr );
  va_end( arg_ptr );
  _logger_ptr( log_type, log_str );
}

/** Helper function to check if a path is a file (i.e. is not a directory). */
static bool _is_file( const char* path ) {
  struct vol_geom_stat64_t path_stat;
  if ( 0 != vol_geom_stat64( path, &path_stat ) ) { return false; }
#ifdef _MSC_VER
  return path_stat.st_mode & _S_IFREG;
#else /* POSIX */
  return S_ISREG( path_stat.st_mode );
#endif
}

/** Helper function to check the actual size of a file on disk.
 * @param filename Input: path to a file.
 * @param sz_ptr   Output: size of the file in bytes.
 * @return         true if a valid file was found and a size could be obtained.
 */
static bool _get_file_sz( const char* filename, vol_geom_size_t* sz_ptr ) {
  struct vol_geom_stat64_t stbuf;
  if ( !_is_file( filename ) ) { return false; }
  if ( 0 != vol_geom_stat64( filename, &stbuf ) ) { return false; }
  *sz_ptr = stbuf.st_size;
  return true;
}

/** Helper function to read an entire file into an array of bytes within struct pointed to by `fr_ptr`.
 * @param max_bytes If zero then read the entire file, otherwise read up to max_bytes into memory.
 */
static bool _read_file( const char* filename, vol_geom_file_record_t* fr_ptr, vol_geom_size_t max_bytes ) {
  FILE* f_ptr = NULL;

  if ( !filename || !fr_ptr ) { goto vol_geom_read_file_failed; }
  if ( !_get_file_sz( filename, &fr_ptr->sz ) ) { goto vol_geom_read_file_failed; }
  fr_ptr->sz = ( 0 == max_bytes || fr_ptr->sz < max_bytes ) ? fr_ptr->sz : max_bytes;

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Allocating %" PRId64 " bytes for reading file\n", fr_ptr->sz );
  fr_ptr->byte_ptr = NULL;
  fr_ptr->byte_ptr = malloc( (size_t)fr_ptr->sz );
  if ( !fr_ptr->byte_ptr ) { goto vol_geom_read_file_failed; }

  f_ptr = fopen( filename, "rb" );
  if ( !f_ptr ) { goto vol_geom_read_file_failed; }
  vol_geom_size_t nr = fread( fr_ptr->byte_ptr, fr_ptr->sz, 1, f_ptr );
  if ( 1 != nr ) { goto vol_geom_read_file_failed; }
  fclose( f_ptr );

  return true;
vol_geom_read_file_failed:
  if ( f_ptr ) { fclose( f_ptr ); }
  if ( fr_ptr ) {
    if ( fr_ptr->byte_ptr ) {
      free( fr_ptr->byte_ptr );
      fr_ptr->byte_ptr = NULL;
    }
    fr_ptr->sz = 0;
  }
  return false;
}

/** Helper function to read Unity-style strings, specified in VOL format, from a loaded file. */
static bool _read_short_str( const uint8_t* data_ptr, uint32_t data_sz, vol_geom_size_t offset, vol_geom_short_str_t* sstr ) {
  if ( !data_ptr || !sstr ) { return false; }
  if ( offset >= data_sz ) { return false; } // OOB

  sstr->sz = data_ptr[offset];               // assumes the 1-byte length
  if ( sstr->sz > 127 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: string length %i given is > 127\n", (int)sstr->sz );
    return false;
  }
  if ( offset + sstr->sz >= data_sz ) { return false; } // OOB
  memcpy( sstr->bytes, &data_ptr[offset + 1], sstr->sz );
  sstr->bytes[sstr->sz] = '\0';

  return true;
}

static bool _read_vol_frame( const vol_geom_info_t* info_ptr, uint32_t frame_idx, vol_geom_frame_data_t* frame_data_ptr ) {
  assert( info_ptr && info_ptr->preallocated_frame_blob_ptr && frame_data_ptr );
  if ( !info_ptr || !info_ptr->preallocated_frame_blob_ptr || !frame_data_ptr ) { return false; }
  if ( frame_idx >= info_ptr->hdr.frame_count ) { return false; }

  *frame_data_ptr = ( vol_geom_frame_data_t ){ .block_data_sz = 0 };

  uint8_t* byte_blob_ptr         = (uint8_t*)info_ptr->preallocated_frame_blob_ptr;
  frame_data_ptr->block_data_ptr = &byte_blob_ptr[info_ptr->frames_directory_ptr[frame_idx].hdr_sz];
  frame_data_ptr->block_data_sz  = info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz;

  {
    // start within the frame's memory but after its frame header and at the start of mesh data
    vol_geom_size_t curr_offset = 0;

    { // vertices
      if ( frame_data_ptr->block_data_sz < ( curr_offset + (vol_geom_size_t)sizeof( uint32_t ) + (vol_geom_size_t)frame_data_ptr->vertices_sz ) ) {
        return false;
      }

      memcpy( &frame_data_ptr->vertices_sz, &frame_data_ptr->block_data_ptr[curr_offset], sizeof( uint32_t ) );
      curr_offset += (vol_geom_size_t)sizeof( uint32_t );
      frame_data_ptr->vertices_offset = curr_offset;
      curr_offset += (vol_geom_size_t)frame_data_ptr->vertices_sz;
    }

    // normals
    if ( info_ptr->hdr.normals && info_ptr->hdr.version >= 11 ) {
      if ( frame_data_ptr->block_data_sz < ( curr_offset + (vol_geom_size_t)sizeof( uint32_t ) + (vol_geom_size_t)frame_data_ptr->normals_sz ) ) {
        return false;
      }

      memcpy( &frame_data_ptr->normals_sz, &frame_data_ptr->block_data_ptr[curr_offset], sizeof( uint32_t ) );
      curr_offset += (vol_geom_size_t)sizeof( uint32_t );
      frame_data_ptr->normals_offset = curr_offset;
      curr_offset += (vol_geom_size_t)frame_data_ptr->normals_sz;
    }

    // indices and UVs
    if ( info_ptr->frame_headers_ptr[frame_idx].keyframe == 1 || ( info_ptr->hdr.version >= 12 && info_ptr->frame_headers_ptr[frame_idx].keyframe == 2 ) ) {
      { // indices
        if ( frame_data_ptr->block_data_sz < ( curr_offset + (vol_geom_size_t)sizeof( uint32_t ) + (vol_geom_size_t)frame_data_ptr->indices_sz ) ) {
          return false;
        }

        memcpy( &frame_data_ptr->indices_sz, &frame_data_ptr->block_data_ptr[curr_offset], sizeof( uint32_t ) );
        curr_offset += (vol_geom_size_t)sizeof( uint32_t );
        frame_data_ptr->indices_offset = curr_offset;
        curr_offset += (vol_geom_size_t)frame_data_ptr->indices_sz;
      }

      { // UVs
        if ( frame_data_ptr->block_data_sz < ( curr_offset + (vol_geom_size_t)sizeof( uint32_t ) + (vol_geom_size_t)frame_data_ptr->uvs_sz ) ) { return false; }

        memcpy( &frame_data_ptr->uvs_sz, &frame_data_ptr->block_data_ptr[curr_offset], sizeof( uint32_t ) );
        curr_offset += (vol_geom_size_t)sizeof( uint32_t );
        frame_data_ptr->uvs_offset = curr_offset;
        curr_offset += (vol_geom_size_t)frame_data_ptr->uvs_sz;
      }
    } // endif indices & UVs

    // texture
    // NOTE(Anton) not tested since we aren't using embedded textures at the moment.
    if ( info_ptr->hdr.version >= 11 && info_ptr->hdr.textured ) {
      if ( frame_data_ptr->block_data_sz < ( curr_offset + (vol_geom_size_t)sizeof( uint32_t ) + (vol_geom_size_t)frame_data_ptr->texture_sz ) ) {
        return false;
      }

      memcpy( &frame_data_ptr->texture_sz, &frame_data_ptr->block_data_ptr[curr_offset], sizeof( uint32_t ) );
      curr_offset += (vol_geom_size_t)sizeof( uint32_t );
      frame_data_ptr->texture_offset = curr_offset;
      curr_offset += (vol_geom_size_t)frame_data_ptr->texture_sz;
    }
  } // endread data sections

  return true;
}

static bool _build_frame_directory_from_file( FILE* f_ptr, vol_geom_info_t* info_ptr, vol_geom_size_t sequence_file_sz, uint32_t frame_idx ) {

  // initialize frame's header struct
  vol_geom_frame_hdr_t frame_hdr = ( vol_geom_frame_hdr_t ){ .mesh_data_sz = 0 };

  // Get the current possition in the file whihch is the start of the frame. 
  vol_geom_size_t frame_start_offset = vol_geom_ftello( f_ptr );
  if ( -1LL == frame_start_offset ) { 
    goto bfdff_fail; 
  }

  if(frame_start_offset + sizeof(vol_geom_frame_hdr_t) > sequence_file_sz) {
    // _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: frame header at frame %i in sequence file was out of file size range.\n", frame_idx );
    goto bfdff_fail;
  }

  if ( !fread( &frame_hdr.frame_number, sizeof( uint32_t ), 1, f_ptr ) ) {
    // _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: frame_number at frame %i in sequence file was out of file size range.\n", frame_idx );
    goto bfdff_fail;
  }
  if ( frame_hdr.frame_number != frame_idx ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: frame_number was %i at frame %i in sequence file.\n", frame_hdr.frame_number, frame_idx );
    goto bfdff_fail;
  }
  if ( !fread( &frame_hdr.mesh_data_sz, sizeof( uint32_t ), 1, f_ptr ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: mesh_data_sz %i was out of file size range in sequence file.\n", frame_hdr.mesh_data_sz );
    goto bfdff_fail;
  }
  if ( (vol_geom_size_t)frame_hdr.mesh_data_sz > sequence_file_sz ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: frame %i has mesh_data_sz %i, which is invalid. Sequence file is %" PRId64 " bytes.\n", frame_idx,
      frame_hdr.mesh_data_sz, sequence_file_sz );
    goto bfdff_fail;
  }
  if ( !fread( &frame_hdr.keyframe, sizeof( uint8_t ), 1, f_ptr ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: keyframe (type) was out of file size range in sequence file.\n" );
    goto bfdff_fail;
  }
  // This will help to seek to keyframe
  if(frame_hdr.keyframe == 1 || frame_hdr.keyframe == 2) {
    frame_hdr.keyframe_number = frame_hdr.frame_number;
  } else {
    // find keyframe. We are building directory sequentially so we can go back, those records should exist.
    // int ret = vol_geom_find_previous_keyframe(info_ptr, frame_idx);
    int ret = info_ptr->frame_headers_ptr[frame_idx-1].keyframe_number;
    if (ret >= 0 ) {
      frame_hdr.keyframe_number = ret;
    } else {
      goto bfdff_fail;
    }
  }

  vol_geom_size_t frame_current_offset = vol_geom_ftello( f_ptr );
  if ( -1LL == frame_current_offset ) { goto bfdff_fail; }
  info_ptr->frames_directory_ptr[frame_idx].hdr_sz = (vol_geom_size_t)frame_current_offset - (vol_geom_size_t)frame_start_offset;

  // in version 12 mesh_data_sz includes array sizes, but earlier versions need to add that to payload size
  info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz = frame_hdr.mesh_data_sz;
  if ( info_ptr->hdr.version < 12 ) {
    // keyframe value 2 only exists in v12 plus but value 1 exists.
    if ( 1 == frame_hdr.keyframe ) {
      info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz += 8; // indices and UVs size
    }
    // version 10 doesn't have normals/texture, but 11 can do.
    if ( 11 == info_ptr->hdr.version ) {
      info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz += 4;   // normals sz
      if ( info_ptr->hdr.textured ) {
        info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz += 4; // texture sz
      }
    }
  }
  if ( info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz > sequence_file_sz ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: frame %i corrected_payload_sz %" PRId64 " bytes was too large for a sequence of %" PRId64 " bytes.\n", frame_idx,
      info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz, sequence_file_sz );
    goto bfdff_fail;
  }

  // Seek past mesh data and past the final integer "frame data size". See if file is big enough.
  if ( 0 != vol_geom_fseeko( f_ptr, info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz + 4, SEEK_CUR ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: not enough memory in sequence file for frame %i contents.\n", frame_idx );
    goto bfdff_fail;
  }
  frame_current_offset = vol_geom_ftello( f_ptr );
  if ( -1LL == frame_current_offset ) { goto bfdff_fail; }

  // Update frame directory and store frame header.
  info_ptr->frames_directory_ptr[frame_idx].offset_sz = (vol_geom_size_t)frame_start_offset;
  info_ptr->frames_directory_ptr[frame_idx].total_sz  = (vol_geom_size_t)frame_current_offset - (vol_geom_size_t)frame_start_offset;
  info_ptr->frame_headers_ptr[frame_idx]              = frame_hdr;
  if ( info_ptr->frames_directory_ptr[frame_idx].total_sz > sequence_file_sz ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: frame %i total_sz %" PRId64 " bytes was too large for a sequence of %" PRId64 " bytes.\n", frame_idx,
      info_ptr->frames_directory_ptr[frame_idx].total_sz, sequence_file_sz );
    goto bfdff_fail;
  }

  if ( info_ptr->frames_directory_ptr[frame_idx].total_sz > info_ptr->biggest_frame_blob_sz ) {
    info_ptr->biggest_frame_blob_sz = info_ptr->frames_directory_ptr[frame_idx].total_sz;
  }
  return true;

bfdff_fail:
  frame_hdr.mesh_data_sz = 0;
  info_ptr->frame_headers_ptr[frame_idx] = frame_hdr;
  // if ( f_ptr ) { fclose( f_ptr ); }
  // if ( info_ptr->frame_headers_ptr ) {
  //   free( info_ptr->frame_headers_ptr );
  //   info_ptr->frame_headers_ptr = NULL;
  // }
  // if ( info_ptr->frames_directory_ptr ) {
  //   free( info_ptr->frames_directory_ptr );
  //   info_ptr->frames_directory_ptr = NULL;
  // }
  return false;
}

/** Find out the size and offset of every frame and store in the info struct pointed to by info_ptr.
 * @param chunk_offset If the sequence chunk is offset into the file then this offset can be provided here, in bytes from the start of the file.
 */
static bool _build_frames_directory_from_file( const char* seq_filename, vol_geom_info_t* info_ptr, vol_geom_size_t chunk_offset ) {
  FILE* f_ptr = NULL;
  if ( !seq_filename || !info_ptr ) { return false; }

  { // Allocate memory for frame headers and frames directory.
    vol_geom_size_t frame_headers_sz = info_ptr->hdr.frame_count * sizeof( vol_geom_frame_hdr_t );
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Allocating %" PRId64 " bytes for frame headers.\n", frame_headers_sz );
    info_ptr->frame_headers_ptr = calloc( 1, (size_t)frame_headers_sz );
    if ( !info_ptr->frame_headers_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating frames headers.\n" );
      goto bfdff_fail;
    }

    vol_geom_size_t frames_directory_sz = info_ptr->hdr.frame_count * sizeof( vol_geom_frame_directory_entry_t );
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Allocating %" PRId64 " bytes for frames directory.\n", frames_directory_sz );
    info_ptr->frames_directory_ptr = calloc( 1, (size_t)frames_directory_sz );
    if ( !info_ptr->frames_directory_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating frames directory.\n" );
      goto bfdff_fail;
    }
  }

  vol_geom_size_t sequence_file_sz = 0;
  if ( !_get_file_sz( seq_filename, &sequence_file_sz ) ) { goto bfdff_fail; }
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Sequence file is %" PRId64 " bytes.\n", sequence_file_sz );

  f_ptr = fopen( seq_filename, "rb" );
  if ( !f_ptr ) { goto bfdff_fail; }
  if ( 0 != vol_geom_fseeko( f_ptr, chunk_offset, SEEK_SET ) ) { goto bfdff_fail; }

  // For every frame in the sequence 
  bool missing_frame_data = false;
  for ( uint32_t i = 0; i < info_ptr->hdr.frame_count; i++ ) {
    
    if(!missing_frame_data) {
      if(_build_frame_directory_from_file(f_ptr, info_ptr, sequence_file_sz, i) == false) {
        // We have probably reached the end of the file due to streaming not finished downloading the file. Try to load it later.
        missing_frame_data = true;
      }
    } else {
      // Initialize header and set the data size to 0.
      vol_geom_frame_hdr_t frame_hdr = ( vol_geom_frame_hdr_t ){ .mesh_data_sz = 0 };
      info_ptr->frame_headers_ptr[i] = frame_hdr;
    }
  }

  fclose( f_ptr );
  f_ptr = NULL; // this is checked later, so make = NULL
  return true;

bfdff_fail:
  _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Failed  building directory, deleting info_ptr.\n" );
  if ( f_ptr ) { fclose( f_ptr ); }
  if ( info_ptr->frame_headers_ptr ) {
    free( info_ptr->frame_headers_ptr );
    info_ptr->frame_headers_ptr = NULL;
  }
  if ( info_ptr->frames_directory_ptr ) {
    free( info_ptr->frames_directory_ptr );
    info_ptr->frames_directory_ptr = NULL;
  }
  return false;
}

/******************************************************************************
  BASIC API
******************************************************************************/

void vol_geom_set_log_callback( void ( *user_function_ptr )( vol_geom_log_type_t log_type, const char* message_str ) ) { _logger_ptr = user_function_ptr; }

void vol_geom_reset_log_callback( void ) { _logger_ptr = _default_logger; }

bool vol_geom_read_hdr_from_mem( const uint8_t* data_ptr, uint32_t data_sz, vol_geom_file_hdr_t* hdr_ptr, vol_geom_size_t* hdr_sz_ptr ) {
  if ( !data_ptr || !hdr_ptr || !hdr_sz_ptr || data_sz < VOL_GEOM_FILE_HDR_V10_MIN_SZ ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "vol_geom_read_hdr_from_mem: invalid parameters\n" );
    return false;
  }

  vol_geom_size_t offset = 0;
  memset( hdr_ptr, 0, sizeof( vol_geom_file_hdr_t ) );

  // Support either Unity format "VOLS" string, or IFF-style first-4-bytes "VOLS" magic file numbers.
  if ( data_ptr[0] == 'V' && data_ptr[1] == 'O' && data_ptr[2] == 'L' && data_ptr[3] == 'S' ) {
    memcpy( hdr_ptr->format.bytes, data_ptr, 4 );
    hdr_ptr->format.sz = 4;
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "vol_geom_read_hdr_from_mem: format: %c, %c, %c, %c\n", data_ptr[0], data_ptr[1], data_ptr[2], data_ptr[3] );
    offset += 4;
  } else {
    if ( !_read_short_str( data_ptr, data_sz, 0, &hdr_ptr->format ) ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "vol_geom_read_hdr_from_mem: failed to read format\n" );
      return false;
    }
    if ( strncmp( "VOLS", hdr_ptr->format.bytes, 4 ) != 0 ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "vol_geom_read_hdr_from_mem: failed format check\n" );
      return false;
    } // Format check.
    offset += ( hdr_ptr->format.sz + 1 );
  }
  if ( offset + 4 * (vol_geom_size_t)sizeof( uint32_t ) + 3 >= data_sz ) { return false; } // OOB
  memcpy( &hdr_ptr->version, &data_ptr[offset], sizeof( uint32_t ) );
  offset += (vol_geom_size_t)sizeof( uint32_t );
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "vol_geom_read_hdr_from_mem: detected header version %d", hdr_ptr->version );
  if ( hdr_ptr->version < 10 || hdr_ptr->version > 13 ) { return false; } // Version check.
  memcpy( &hdr_ptr->compression, &data_ptr[offset], sizeof( uint32_t ) );
  offset += (vol_geom_size_t)sizeof( uint32_t );
  if ( hdr_ptr->version < 13 ) { // V1.3 removed strings & topology field from header.
    if ( !_read_short_str( data_ptr, data_sz, offset, &hdr_ptr->mesh_name ) ) { return false; }
    offset += ( hdr_ptr->mesh_name.sz + 1 );
    if ( offset + 2 * (vol_geom_size_t)sizeof( uint32_t ) + 2 >= data_sz ) { return false; } // OOB
    if ( !_read_short_str( data_ptr, data_sz, offset, &hdr_ptr->material ) ) { return false; }
    offset += ( hdr_ptr->material.sz + 1 );
    if ( offset + 2 * (vol_geom_size_t)sizeof( uint32_t ) + 1 >= data_sz ) { return false; } // OOB
    if ( !_read_short_str( data_ptr, data_sz, offset, &hdr_ptr->shader ) ) { return false; }
    offset += ( hdr_ptr->shader.sz + 1 );
    if ( offset + 2 * (vol_geom_size_t)sizeof( uint32_t ) >= data_sz ) { return false; } // OOB
    memcpy( &hdr_ptr->topology, &data_ptr[offset], sizeof( uint32_t ) );
    offset += (vol_geom_size_t)sizeof( uint32_t );
  }
  memcpy( &hdr_ptr->frame_count, &data_ptr[offset], sizeof( uint32_t ) );
  offset += (vol_geom_size_t)sizeof( uint32_t );
  // Parse v1.1 part of header.
  if ( hdr_ptr->version < 11 ) { goto vol_geom_rhfmem_success; }
  const vol_geom_size_t v11_section_sz = (vol_geom_size_t)( 3 * sizeof( uint16_t ) + 2 * sizeof( uint8_t ) );
  if ( offset + v11_section_sz > data_sz ) { return false; } // OOB
  hdr_ptr->normals  = (bool)data_ptr[offset++];
  hdr_ptr->textured = (bool)data_ptr[offset++];

  if ( hdr_ptr->version >= 13 ) {                           // v1.3 added texture compression fields.
    hdr_ptr->texture_compression      = data_ptr[offset++]; // { 0 = mp4, 1 = ETC1S, 2 = UASTC }
    hdr_ptr->texture_container_format = data_ptr[offset++]; // { 0 = raw, 1 = basis, 2 = KTX2 }
    memcpy( &hdr_ptr->texture_width, &data_ptr[offset], sizeof( uint32_t ) );
    offset += (vol_geom_size_t)sizeof( uint32_t );
    memcpy( &hdr_ptr->texture_height, &data_ptr[offset], sizeof( uint32_t ) );
    offset += (vol_geom_size_t)sizeof( uint32_t );
    memcpy( &hdr_ptr->fps, &data_ptr[offset], sizeof( float ) );
    offset += (vol_geom_size_t)sizeof( float );
    memcpy( &hdr_ptr->audio, &data_ptr[offset], sizeof( uint32_t ) );
    offset += (vol_geom_size_t)sizeof( uint32_t );
    memcpy( &hdr_ptr->audio_start, &data_ptr[offset], sizeof( uint32_t ) );
    offset += (vol_geom_size_t)sizeof( uint32_t );
    memcpy( &hdr_ptr->frame_body_start, &data_ptr[offset], sizeof( uint32_t ) );
    offset += (vol_geom_size_t)sizeof( uint32_t );
    if ( offset != 44 ) { return false; }
    goto vol_geom_rhfmem_success; // End of header for v1.3 here.
  }
  uint16_t w = 0, h = 0;
  memcpy( &w, &data_ptr[offset], sizeof( uint16_t ) );
  offset += (vol_geom_size_t)sizeof( uint16_t );
  memcpy( &h, &data_ptr[offset], sizeof( uint16_t ) );
  offset += (vol_geom_size_t)sizeof( uint16_t );
  hdr_ptr->texture_width  = (uint32_t)w;
  hdr_ptr->texture_height = (uint32_t)h;
  memcpy( &hdr_ptr->texture_format, &data_ptr[offset], sizeof( uint16_t ) );
  offset += (vol_geom_size_t)sizeof( uint16_t );
  // Parse v1.2 part of header.
  if ( hdr_ptr->version < 12 ) { goto vol_geom_rhfmem_success; }
  const vol_geom_size_t v12_section_sz = 8 * sizeof( float );
  if ( offset + v12_section_sz > data_sz ) { return false; } // OOB
  memcpy( hdr_ptr->translation, &data_ptr[offset], 3 * sizeof( float ) );
  offset += 3 * sizeof( float );
  memcpy( hdr_ptr->rotation, &data_ptr[offset], 4 * sizeof( float ) );
  offset += 4 * sizeof( float );
  memcpy( &hdr_ptr->scale, &data_ptr[offset], sizeof( float ) );
  offset += sizeof( float );

vol_geom_rhfmem_success:
  *hdr_sz_ptr = offset;
  return true;
}

bool vol_geom_read_hdr_from_file( const char* filename, vol_geom_file_hdr_t* hdr_ptr, vol_geom_size_t* hdr_sz_ptr ) {
  vol_geom_file_record_t record = ( vol_geom_file_record_t ){ .byte_ptr = NULL };
  if ( !filename || !hdr_ptr || !hdr_sz_ptr ) { return false; }
  if ( !_read_file( filename, &record, sizeof( vol_geom_file_hdr_t ) ) ) { goto rhff_fail; }
  if ( !vol_geom_read_hdr_from_mem( record.byte_ptr, record.sz, hdr_ptr, hdr_sz_ptr ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_read_hdr_from_file: Failed to read header from file `%s`.\n", filename );
    goto rhff_fail;
  }
  if ( record.byte_ptr != NULL ) { free( record.byte_ptr ); }
  return true;
rhff_fail:
  if ( record.byte_ptr != NULL ) { free( record.byte_ptr ); }
  return false;
}

bool vol_geom_read_audio_from_file( const char* vols_filename, vol_geom_info_t* info_ptr ) {
  FILE* f_ptr = NULL;
  if ( !vols_filename || !info_ptr || info_ptr->hdr.version < 13 ) { goto vgraff_fail; }

  f_ptr = fopen( vols_filename, "rb" );
  if ( !f_ptr ) { goto vgraff_fail; }
  if ( 0 != vol_geom_fseeko( f_ptr, info_ptr->hdr.audio_start, SEEK_SET ) ) { goto vgraff_fail; }
  if ( 1 != fread( &info_ptr->audio_data_sz, sizeof( uint32_t ), 1, f_ptr ) ) { goto vgraff_fail; }
  info_ptr->audio_data_ptr = malloc( info_ptr->audio_data_sz );
  if ( !info_ptr->audio_data_ptr ) { goto vgraff_fail; }
  if ( 1 != fread( info_ptr->audio_data_ptr, info_ptr->audio_data_sz, 1, f_ptr ) ) { goto vgraff_fail; }
  fclose( f_ptr );
  return true;
vgraff_fail:
  if ( f_ptr ) { fclose( f_ptr ); }
  return false;
}

bool vol_geom_create_file_info_from_file( const char* vols_filename, vol_geom_info_t* info_ptr ) {
  if ( !vols_filename || !info_ptr || !_is_file( vols_filename ) ) { return false; }

  vol_geom_size_t hdr_sz = 0;
  if ( !vol_geom_read_hdr_from_file( vols_filename, &info_ptr->hdr, &hdr_sz ) ) { goto cfiff_fail; }
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "Vologram header v%i.%i\n", info_ptr->hdr.version / 10, info_ptr->hdr.version % 10 );

  if ( info_ptr->hdr.audio && !vol_geom_read_audio_from_file( vols_filename, info_ptr ) ) { goto cfiff_fail; }

  // v1.3 introduced a header offset field for this. Preceding versions are immediately after the header.
  info_ptr->sequence_offset = info_ptr->hdr.frame_body_start ? info_ptr->hdr.frame_body_start : hdr_sz;

  if ( !_build_frames_directory_from_file( vols_filename, info_ptr, info_ptr->sequence_offset ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_create_file_info_from_file(): Failed to create frames directory.\n" );
    goto cfiff_fail;
  }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Allocating preallocated_frame_blob_ptr bytes %" PRId64 "\n", info_ptr->biggest_frame_blob_sz );
  if ( info_ptr->biggest_frame_blob_sz >= 1024 * 1024 * 1024 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: extremely high frame size %" PRId64 " reported - assuming error.\n", info_ptr->biggest_frame_blob_sz );
    goto cfiff_fail;
  }
  info_ptr->preallocated_frame_blob_ptr = calloc( 1, info_ptr->biggest_frame_blob_sz );
  if ( !info_ptr->preallocated_frame_blob_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: out of memory allocating frame blob reserve.\n" );
    goto cfiff_fail;
  }

  return true;

cfiff_fail:
  vol_geom_free_file_info( info_ptr );
  return false;
}

bool vol_geom_create_file_info( const char* hdr_filename, const char* seq_filename, vol_geom_info_t* info_ptr, bool streaming_mode ) {
  if ( !hdr_filename || !seq_filename || !info_ptr ) { return false; }

  vol_geom_file_record_t record = ( vol_geom_file_record_t ){ .sz = 0 };
  vol_geom_size_t hdr_sz        = 0;
  *info_ptr                     = ( vol_geom_info_t ){ .biggest_frame_blob_sz = 0, .last_keyframe = -1 };
  info_ptr->sequence_offset     = 0; // Using separate files here, so there is no offset.

  if ( !vol_geom_read_hdr_from_file( hdr_filename, &info_ptr->hdr, &hdr_sz ) ) { goto cfi_fail; }

  if ( !_build_frames_directory_from_file( seq_filename, info_ptr, info_ptr->sequence_offset ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_create_file_info_from_file(): Failed to create frames directory.\n" );
    goto cfi_fail;
  }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Allocating preallocated_frame_blob_ptr bytes %" PRId64 "\n", info_ptr->biggest_frame_blob_sz );
  if ( info_ptr->biggest_frame_blob_sz >= 1024 * 1024 * 1024 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: extremely high frame size %" PRId64 " reported - assuming error.\n", info_ptr->biggest_frame_blob_sz );
    goto cfi_fail;
  }
  info_ptr->preallocated_frame_blob_ptr = calloc( 1, info_ptr->biggest_frame_blob_sz );
  if ( !info_ptr->preallocated_frame_blob_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: out of memory allocating frame blob reserve.\n" );
    goto cfi_fail;
  }

  // If not dealing with huge sequence files - preload the whole thing to memory to avoid file I/O problems.
  if ( !streaming_mode ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Reading entire sequence file to blob memory\n" );
    vol_geom_file_record_t seq_blob = ( vol_geom_file_record_t ){ .sz = 0 };
    if ( !_read_file( seq_filename, &seq_blob, 0 ) ) { goto cfi_fail; }
    info_ptr->sequence_blob_byte_ptr = (uint8_t*)seq_blob.byte_ptr;
  }

  return true;

cfi_fail:

  _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Failed to parse info from vologram geometry files.\n" );
  if ( record.byte_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Freeing record.byte_ptr\n" );
    free( record.byte_ptr );
  }
  vol_geom_free_file_info( info_ptr );

  return false;
}

bool vol_geom_free_file_info( vol_geom_info_t* info_ptr ) {
  if ( !info_ptr ) { return false; }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Freeing allocated vol_geom info_ptr memory.\n" );
  if ( info_ptr->audio_data_ptr ) { free( info_ptr->audio_data_ptr ); }
  if ( info_ptr->sequence_blob_byte_ptr ) { free( info_ptr->sequence_blob_byte_ptr ); }
  if ( info_ptr->preallocated_frame_blob_ptr ) { free( info_ptr->preallocated_frame_blob_ptr ); }
  if ( info_ptr->frame_headers_ptr ) { free( info_ptr->frame_headers_ptr ); }
  if ( info_ptr->frames_directory_ptr ) { free( info_ptr->frames_directory_ptr ); }
  
  // Clean up streaming buffer if it exists
  if ( info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Freeing streaming ring buffer memory.\n" );
    if ( info_ptr->streaming_buffer_ptr->ring_buffer ) {
      free( info_ptr->streaming_buffer_ptr->ring_buffer );
    }
    if ( info_ptr->streaming_buffer_ptr->frames ) {
      free( info_ptr->streaming_buffer_ptr->frames );
    }
    free( info_ptr->streaming_buffer_ptr );
  }
  
  *info_ptr = ( vol_geom_info_t ){ .hdr.frame_count = 0 };

  return true;
}

bool vol_geom_is_keyframe( const vol_geom_info_t* info_ptr, uint32_t frame_idx ) {
  assert( info_ptr );
  if ( !info_ptr ) { return false; }
  if ( frame_idx >= info_ptr->hdr.frame_count ) { return false; }
  if ( 0 == info_ptr->frame_headers_ptr[frame_idx].keyframe ) { return false; }
  return true;
}

int vol_geom_find_previous_keyframe( const vol_geom_info_t* info_ptr, uint32_t frame_idx ) {
  assert( info_ptr );
  if ( !info_ptr ) { return -1; }
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: info_ptr is not NULL.\n" );
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: frame_idx is %i.\n", frame_idx );
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: hdr.frame_count is %i.\n", info_ptr->hdr.frame_count );
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: frame_headers_ptr is %p.\n", info_ptr->frame_headers_ptr );
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: frame_headers_ptr[frame_idx].keyframe is %i.\n", info_ptr->frame_headers_ptr[frame_idx].keyframe );
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: frame_headers_ptr[frame_idx].mesh_data_sz is %i.\n", info_ptr->frame_headers_ptr[frame_idx].mesh_data_sz );
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: frame_headers_ptr[frame_idx].keyframe_number is %i.\n", info_ptr->frame_headers_ptr[frame_idx].keyframe_number );

  if ( frame_idx >= info_ptr->hdr.frame_count ) { 
    _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe: frame_idx is out of bounds.\n" );
    return -1; 
  }
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe from mesh_data_sz is %i .\n", info_ptr->frame_headers_ptr[frame_idx].mesh_data_sz );
  if ( info_ptr->frame_headers_ptr[frame_idx].mesh_data_sz > 0 ) { 
    _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe from keyframe_number is %i .\n", info_ptr->frame_headers_ptr[frame_idx].keyframe_number );
    
    return info_ptr->frame_headers_ptr[frame_idx].keyframe_number;
  }
  return -1;
}

bool vol_geom_update_frames_directory( const char* seq_filename, vol_geom_info_t* info_ptr, uint32_t frame_idx) {
  
  if(frame_idx < 0) return false;

  // early exit if we have a directory record
  if(info_ptr->frame_headers_ptr[frame_idx].mesh_data_sz != 0) return true;

  // Get file size and check for file size issues before allocating memory or reading
  vol_geom_size_t file_sz = 0;
  if ( !_get_file_sz( seq_filename, &file_sz ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: sequence file `%s` could not be opened.\n", seq_filename );
    return false;
  }
  // open file to read the frame
  FILE* f_ptr = fopen( seq_filename, "rb" );
  if ( !f_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR could not open file `%s` for frame data.\n", seq_filename );
    return false;
  }

  vol_geom_size_t biggest_frame_blob_sz = info_ptr->biggest_frame_blob_sz;

  // Find the last frame that has a good directory item and start filling it until we reach curent frame
  uint32_t last_idx = frame_idx - 1;
  for(; last_idx >= 0; --last_idx ) {
    if(info_ptr->frame_headers_ptr[last_idx].mesh_data_sz != 0) {
      break;
    }
  }

  // move to the end of a known directory record
  vol_geom_size_t last_offset_end = ((int)last_idx < 0)? info_ptr->sequence_offset : \
                              info_ptr->frames_directory_ptr[last_idx].offset_sz + info_ptr->frames_directory_ptr[last_idx].total_sz;

  if ( 0 != vol_geom_fseeko( f_ptr, last_offset_end, SEEK_SET ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR seeking frame %i from sequence file - file too small for data.\n", last_idx );
    fclose( f_ptr );
    return false;
  }

  for(last_idx += 1; last_idx < info_ptr->hdr.frame_count; ++last_idx ) {
    // _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "INFO Building directory for frame %i\n", last_idx );

    if(_build_frame_directory_from_file( f_ptr, info_ptr,  file_sz,  last_idx ) == false) {
      if(frame_idx <= last_idx) {
        // we got a record for our frame, we can continue
        break;
      }
      // data for current frame are not there
      fclose( f_ptr );
      return false;
    }
  }
  fclose( f_ptr );

  // Update maximum blob size and its allocation in the memory
  if(info_ptr->biggest_frame_blob_sz > biggest_frame_blob_sz ) {
    if ( info_ptr->preallocated_frame_blob_ptr ) { 
      info_ptr->preallocated_frame_blob_ptr = realloc( info_ptr->preallocated_frame_blob_ptr, info_ptr->biggest_frame_blob_sz ); 
    } else {
      info_ptr->preallocated_frame_blob_ptr = calloc( 1, info_ptr->biggest_frame_blob_sz );
    }
    if ( !info_ptr->preallocated_frame_blob_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: out of memory allocating frame blob reserve.\n" );
      return false;
    }
  }

  return true;
}

bool vol_geom_read_frame( const char* seq_filename,  vol_geom_info_t* info_ptr, uint32_t frame_idx, vol_geom_frame_data_t* frame_data_ptr ) {
  assert( seq_filename && info_ptr && frame_data_ptr );
  if ( !seq_filename || !info_ptr || !frame_data_ptr ) { return false; }

  if ( frame_idx >= info_ptr->hdr.frame_count ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: frame requested (%i) is not in valid range of 0-%i for sequence\n", frame_idx, info_ptr->hdr.frame_count );
    return false;
  }

  // Find frame section within sequence file blob if it was pre-loaded.
  if ( info_ptr->sequence_blob_byte_ptr ) {
    // Get the offset of that frame and size required to allocate for it.
    vol_geom_size_t offset_sz = info_ptr->frames_directory_ptr[frame_idx].offset_sz;
    vol_geom_size_t total_sz  = info_ptr->frames_directory_ptr[frame_idx].total_sz;

    if ( info_ptr->biggest_frame_blob_sz < total_sz ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: pre-allocated frame blob was too small for frame %i: %" PRId64 "/%" PRId64 " bytes.\n", frame_idx,
      info_ptr->biggest_frame_blob_sz, total_sz );
      return false;
    }
    memcpy( info_ptr->preallocated_frame_blob_ptr, &info_ptr->sequence_blob_byte_ptr[offset_sz], total_sz );

  // Read frame blob from file.
  } else {

    // if the frame directory wasn't created yet for frame_idx, do it now
    if(info_ptr->frame_headers_ptr[frame_idx].mesh_data_sz == 0) {
      if(vol_geom_update_frames_directory(seq_filename, info_ptr, frame_idx) == false) {
        return false;
      }
    }

    // Get file size and check for file size issues before allocating memory or reading
    vol_geom_size_t file_sz = 0;
    if ( !_get_file_sz( seq_filename, &file_sz ) ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: sequence file `%s` could not be opened.\n", seq_filename );
      return false;
    }
    // open file to read the frame
    FILE* f_ptr = fopen( seq_filename, "rb" );
    if ( !f_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR could not open file `%s` for frame data.\n", seq_filename );
      return false;
    }

    // Get the offset of that frame and size required to allocate for it.
    vol_geom_size_t offset_sz = info_ptr->frames_directory_ptr[frame_idx].offset_sz;
    vol_geom_size_t total_sz  = info_ptr->frames_directory_ptr[frame_idx].total_sz;

    if ( file_sz < ( offset_sz + total_sz ) ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: sequence file is too short to contain frame %i data.\n", frame_idx );
      return false;
    }

    if ( info_ptr->biggest_frame_blob_sz < total_sz ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: pre-allocated frame blob was too small for frame %i: %" PRId64 "/%" PRId64 " bytes.\n", frame_idx,
        info_ptr->biggest_frame_blob_sz, total_sz );
      return false;
    }


    if ( 0 != vol_geom_fseeko( f_ptr, offset_sz, SEEK_SET ) ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR seeking frame %i from sequence file - file too small for data.\n", frame_idx );
      fclose( f_ptr );
      return false;
    }
    if ( !fread( info_ptr->preallocated_frame_blob_ptr, total_sz, 1, f_ptr ) ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR reading frame %i from sequence file\n", frame_idx );
      fclose( f_ptr );
      return false;
    }
    fclose( f_ptr );
  } // end FILE i/o block

  // Brute force
  // if we miss indices and UVs from a keyframe that was skipped. Current_frame should be frame_idx-1 otherwise our indices might be wrong.
  if(info_ptr->frame_headers_ptr[frame_idx].keyframe == 0 && info_ptr->last_keyframe != info_ptr->frame_headers_ptr[frame_idx].keyframe_number) {
    uint32_t keyframe_number = info_ptr->frame_headers_ptr[frame_idx].keyframe_number;
    
    // Web player should never get here, it has its own solution for this.
    _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO loading keyframe %i for frame %i.\n", keyframe_number, frame_idx );

    if ( !vol_geom_read_frame( seq_filename,  info_ptr, keyframe_number, frame_data_ptr ) ) {
      // we have a problem
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR reading key frame %i\n", frame_idx );
    }
  }

  if ( !_read_vol_frame( info_ptr, frame_idx, frame_data_ptr ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR parsing frame %i\n", frame_idx );
    return false;
  } else {
    // Remember we loaded a keyframe 
    if(info_ptr->frame_headers_ptr[frame_idx].keyframe == 1) info_ptr->last_keyframe = frame_idx;
  }
  return true;
}

//
// ===== STREAMING BUFFER IMPLEMENTATION =====
// Implementation of the streaming buffer API for large file support.
//

bool vol_geom_init_streaming_config( vol_geom_streaming_config_t* config_ptr ) {
  if ( !config_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_init_streaming_config() - config_ptr is NULL.\n" );
    return false;
  }

  // Set default values based on our design specifications
  config_ptr->max_buffer_size = 200 * 1024 * 1024;  // 200MB default
  config_ptr->min_buffer_size = 50 * 1024 * 1024;   // 50MB minimum  
  config_ptr->reserved_space_size = 10 * 1024 * 1024; // 10MB for first frame and keyframes
  config_ptr->auto_select_mode = true;               // Auto-select by default
  config_ptr->force_streaming_mode = false;          // Don't force by default
  config_ptr->lookahead_seconds = 2.0f;              // 2 seconds lookahead

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Streaming config initialized: max_buffer=%.1fMB, min_buffer=%.1fMB, reserved=%.1fMB, lookahead=%.1fs\n",
    config_ptr->max_buffer_size / (1024.0 * 1024.0),
    config_ptr->min_buffer_size / (1024.0 * 1024.0), 
    config_ptr->reserved_space_size / (1024.0 * 1024.0),
    config_ptr->lookahead_seconds );

  return true;
}

bool vol_geom_should_use_streaming_mode( vol_geom_size_t file_size, const vol_geom_streaming_config_t* config_ptr ) {
  if ( !config_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_should_use_streaming_mode() - config_ptr is NULL.\n" );
    return false;
  }

  // If streaming mode is forced, always return true
  if ( config_ptr->force_streaming_mode ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Using streaming mode (forced by configuration).\n" );
    return true;
  }

  // If file size is unknown (0 or negative), default to streaming mode for safety
  if ( file_size <= 0 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Using streaming mode (unknown file size).\n" );
    return true;
  }

  // If auto-selection is disabled, default to full download
  if ( !config_ptr->auto_select_mode ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Using full download mode (auto-selection disabled).\n" );
    return false;
  }

  // Auto-selection logic: use streaming if file is larger than max buffer size
  bool use_streaming = file_size > config_ptr->max_buffer_size;
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "File size: %.1fMB, max buffer: %.1fMB -> Using %s mode.\n",
    file_size / (1024.0 * 1024.0),
    config_ptr->max_buffer_size / (1024.0 * 1024.0),
    use_streaming ? "streaming" : "full download" );

  return use_streaming;
}

bool vol_geom_parse_frame_header_from_buffer( const uint8_t* buffer_ptr, vol_geom_size_t offset, vol_geom_frame_hdr_t* header_ptr, vol_geom_size_t* header_size_ptr ) {
  if ( !buffer_ptr || !header_ptr || !header_size_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_parse_frame_header_from_buffer() - NULL pointer(s).\n" );
    return false;
  }

  // Calculate the size of a frame header - this matches the existing vol_geom_frame_hdr_t structure
  const vol_geom_size_t frame_header_size = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);
  *header_size_ptr = frame_header_size;

  // Parse the frame header from the buffer at the specified offset
  const uint8_t* read_ptr = buffer_ptr + offset;

  // Read frame_number (4 bytes, little-endian)
  header_ptr->frame_number = *((uint32_t*)read_ptr);
  read_ptr += sizeof(uint32_t);

  // Read mesh_data_sz (4 bytes, little-endian) 
  header_ptr->mesh_data_sz = *((uint32_t*)read_ptr);
  read_ptr += sizeof(uint32_t);

  // // Read keyframe_number (4 bytes, little-endian)
  header_ptr->keyframe_number = 0;  
  // read_ptr += sizeof(uint32_t);

  // Read keyframe type (1 byte)
  header_ptr->keyframe = *read_ptr;

  // Validate the parsed header
  if ( header_ptr->mesh_data_sz == 0 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Parsed frame %u has mesh_data_sz of 0.\n", header_ptr->frame_number );
    return false;
  }

  if ( header_ptr->mesh_data_sz > 100 * 1024 * 1024 ) { // Sanity check: 100MB max per frame
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Parsed frame %u has unrealistic mesh_data_sz of %u bytes.\n", 
      header_ptr->frame_number, header_ptr->mesh_data_sz );
    return false;
  }

  if ( header_ptr->keyframe > 2 ) { // Valid keyframe values are 0, 1, 2
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Parsed frame %u has invalid keyframe type %u.\n", 
      header_ptr->frame_number, header_ptr->keyframe );
    return false;
  }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Parsed frame header: frame=%u, mesh_size=%u, keyframe_type=%u\n",
    header_ptr->frame_number, header_ptr->mesh_data_sz, header_ptr->keyframe );

  return true;
}

bool vol_geom_create_streaming_buffer( vol_geom_info_t* info_ptr, const vol_geom_streaming_config_t* config_ptr ) {
  if ( !info_ptr || !config_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_create_streaming_buffer() - NULL pointer(s).\n" );
    return false;
  }

  // Validate buffer size
  if ( config_ptr->max_buffer_size < config_ptr->min_buffer_size ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: max_buffer_size (%.1fMB) is smaller than min_buffer_size (%.1fMB).\n",
      config_ptr->max_buffer_size / (1024.0 * 1024.0), config_ptr->min_buffer_size / (1024.0 * 1024.0) );
    return false;
  }

  // Allocate the buffer state structure
  info_ptr->streaming_buffer_ptr = calloc( 1, sizeof(vol_geom_buffer_state_t) );
  if ( !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating streaming buffer state.\n" );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  // Copy configuration
  buffer_state->config = *config_ptr;
  buffer_state->ring_capacity = config_ptr->max_buffer_size;

  // Initialize sequence_offset to 0 - will be determined when parsing main header
  info_ptr->sequence_offset = 0;

  // Allocate single ring buffer
  buffer_state->ring_buffer = calloc( 1, buffer_state->ring_capacity );
  if ( !buffer_state->ring_buffer ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating ring buffer of %.1fMB.\n",
      buffer_state->ring_capacity / (1024.0 * 1024.0) );
    free( info_ptr->streaming_buffer_ptr );
    info_ptr->streaming_buffer_ptr = NULL;
    return false;
  }

  // Estimate max frames tracked (assume average 1MB per frame)
  const vol_geom_size_t estimated_avg_frame_size = 1024 * 1024;  // 1MB
  buffer_state->max_frames_per_buffer = (uint32_t)(buffer_state->ring_capacity / estimated_avg_frame_size);
  if ( buffer_state->max_frames_per_buffer > 500 ) {
    buffer_state->max_frames_per_buffer = 500;  // Cap at reasonable limit
  }
  if ( buffer_state->max_frames_per_buffer < 10 ) {
    buffer_state->max_frames_per_buffer = 10;  // Minimum frames
  }

  // Allocate frame directory
  buffer_state->frames = calloc( buffer_state->max_frames_per_buffer, sizeof(vol_geom_buffer_frame_info_t) );
  if ( !buffer_state->frames ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating frame directory.\n" );
    free( buffer_state->ring_buffer );
    free( info_ptr->streaming_buffer_ptr );
    info_ptr->streaming_buffer_ptr = NULL;
    return false;
  }

  // Initialize state
  buffer_state->data_size = 0;
  buffer_state->parse_pos = 0;
  buffer_state->frame_count = 0;
  buffer_state->file_pos = 0;
  buffer_state->file_size = 0; // Will be set when known
  buffer_state->is_streaming_mode = true;
  buffer_state->last_playback_frame = 0;

  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "Created streaming ring buffer: %.1fMB, %u max frames tracked.\n",
    buffer_state->ring_capacity / (1024.0 * 1024.0), buffer_state->max_frames_per_buffer );

  return true;
}

bool vol_geom_add_data_to_buffer( vol_geom_info_t* info_ptr, const uint8_t* data_ptr, vol_geom_size_t data_size ) {
  if ( !info_ptr || !data_ptr || data_size <= 0 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_add_data_to_buffer() - invalid parameters.\n" );
    return false;
  }

  if ( !info_ptr->streaming_buffer_ptr || !info_ptr->streaming_buffer_ptr->ring_buffer ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: streaming ring buffer not initialized.\n" );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  
  // Check space in ring (linear tail). If not enough, compact by evicting from the front at the last fully consumed frame boundary.
  vol_geom_size_t free_space = buffer_state->ring_capacity - buffer_state->data_size;
  if ( data_size > free_space ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_WARNING, "Not enough space in ring buffer: need %" PRId64 " bytes, have %" PRId64 " bytes.\n", data_size, free_space );
    return false; // JS should pause
  }

  // Append linearly at [data_size, data_size+data_size)
  memcpy( buffer_state->ring_buffer + buffer_state->data_size, data_ptr, data_size );
  buffer_state->data_size += data_size;
  buffer_state->file_pos += data_size;

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Added %" PRId64 " bytes to ring. Now %" PRId64 "/%" PRId64 ". file_pos=%" PRId64 "\n",
    data_size, buffer_state->data_size, buffer_state->ring_capacity, buffer_state->file_pos );

  return true;
}

bool vol_geom_is_frame_available_in_buffer( const vol_geom_info_t* info_ptr, uint32_t frame_idx ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    return false; // Not in streaming mode or buffer not initialized
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  for ( uint32_t i = 0; i < buffer_state->frame_count; i++ ) {
    if ( buffer_state->frames[i].frame_number == frame_idx ) { return true; }
  }
  return false;
}

vol_geom_size_t vol_geom_get_buffer_health_bytes( const vol_geom_info_t* info_ptr ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    return 0; // Not in streaming mode or buffer not initialized
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  return buffer_state->data_size - (info_ptr->sequence_offset ? info_ptr->sequence_offset : 0);
}

float vol_geom_get_buffer_health_seconds( const vol_geom_info_t* info_ptr, float fps ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr || fps <= 0.0f ) {
    return 0.0f;
  }

  // Get bytes available and estimate seconds 
  // This is a rough estimate until we implement proper frame counting for dual buffer
  vol_geom_size_t available_bytes = vol_geom_get_buffer_health_bytes( info_ptr );
  
  // Rough estimate: assume 1MB per frame (will be more accurate once we parse frames)
  const vol_geom_size_t estimated_bytes_per_frame = 1024 * 1024;
  float estimated_frames = (float)available_bytes / (float)estimated_bytes_per_frame;
  float estimated_seconds = estimated_frames / fps;

  return estimated_seconds;
}

bool vol_geom_should_resume_download( const vol_geom_info_t* info_ptr, uint32_t current_frame, float fps ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr || fps <= 0.0f ) {
    return true; // Default to resume if not in streaming mode
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  // Track the most recent playback frame so compaction can evict safely
  buffer_state->last_playback_frame = current_frame;

  // Calculate buffer health in seconds
  float buffer_health_seconds = vol_geom_get_buffer_health_seconds( info_ptr, fps );

  // Resume download if buffer health is below the lookahead threshold
  bool should_resume = buffer_health_seconds < buffer_state->config.lookahead_seconds;
  
  // Also check if ring is near full; if so, don't resume
  vol_geom_size_t full_threshold = (buffer_state->ring_capacity * 9) / 10;
  if ( buffer_state->data_size >= full_threshold ) { should_resume = false; }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Buffer health: %.1fs, lookahead: %.1fs -> %s download\n",
    buffer_health_seconds, buffer_state->config.lookahead_seconds, should_resume ? "resume" : "pause" );

  return should_resume;
}

bool vol_geom_update_buffer_frame_directory( vol_geom_info_t* info_ptr ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_update_buffer_frame_directory() - invalid parameters.\n" );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  // Reuse single-buffer parser over the ring's linear data [0, data_size)
  bool updated = vol_geom_update_single_buffer_frames( info_ptr,
    buffer_state->ring_buffer,
    buffer_state->data_size,
    buffer_state->frames,
    &buffer_state->frame_count,
    "RING" );
  // Mirror ALL frames known in the ring directory into the standard arrays so higher-level APIs see them,
  // even if no new frames were parsed in this call (e.g., arrays were allocated after initial parsing).
  if ( info_ptr->frames_directory_ptr && info_ptr->frame_headers_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "MIRROR_DEBUG: mirroring %u frames from ring to standard arrays (updated=%i)\n",
      buffer_state->frame_count, updated ? 1 : 0 );
    for ( uint32_t i = 0; i < buffer_state->frame_count; i++ ) {
      uint32_t f = buffer_state->frames[i].frame_number;
      if ( f < info_ptr->hdr.frame_count ) {
        info_ptr->frames_directory_ptr[f].hdr_sz = buffer_state->frames[i].header_size;
        info_ptr->frames_directory_ptr[f].corrected_payload_sz = buffer_state->frames[i].mesh_data_size;
        info_ptr->frames_directory_ptr[f].total_sz = buffer_state->frames[i].frame_size;
        info_ptr->frames_directory_ptr[f].offset_sz = 0; // Not used in streaming
        // Ensure frame_headers_ptr has mesh size set for availability checks
        if ( info_ptr->frame_headers_ptr[f].mesh_data_sz == 0 ) {
          info_ptr->frame_headers_ptr[f].mesh_data_sz = buffer_state->frames[i].mesh_data_size;
          info_ptr->frame_headers_ptr[f].frame_number = f;
          // Assume frame 0 is a keyframe if unknown; others keep existing value
          if ( f == 0 && info_ptr->frame_headers_ptr[f].keyframe == 0 ) {
            info_ptr->frame_headers_ptr[f].keyframe = 1;
          }
        }
        // Always propagate keyframe number/type from parsed header when available
        if ( buffer_state->frames[i].header_size > 0 ) {
          // header_size > 0 implies we parsed a header for this frame
          // frame_headers_ptr[f] may already exist; refresh fields that can change due to wrap/compaction
          info_ptr->frame_headers_ptr[f].keyframe = info_ptr->frame_headers_ptr[f].keyframe; // keep existing if set later
        }
      }
    }
  } else {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "MIRROR_DEBUG: standard arrays not yet allocated; skipping mirror.\n" );
  }
  return updated;
}

// Helper function to parse frames from a single buffer and update unified directory
bool vol_geom_update_single_buffer_frames( vol_geom_info_t* info_ptr, uint8_t* buffer_to_parse, 
                                          vol_geom_size_t buffer_data_size, 
                                          vol_geom_buffer_frame_info_t* frame_directory, 
                                          uint32_t* frame_count, const char* buffer_name ) {
  
  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Parsing buffer %s: data_size=%" PRId64 "\n", buffer_name, buffer_data_size );
  
  // If we don't have the main header yet, try to parse it (only once)
  if ( info_ptr->hdr.frame_count == 0 && info_ptr->sequence_offset == 0 && buffer_data_size > VOL_GEOM_FILE_HDR_V10_MIN_SZ ) {
    vol_geom_size_t hdr_sz = 0;
    vol_geom_file_hdr_t temp_hdr;
    
    if ( vol_geom_read_hdr_from_mem( buffer_to_parse, (uint32_t)buffer_data_size, &temp_hdr, &hdr_sz ) ) {
      info_ptr->hdr = temp_hdr;
      info_ptr->sequence_offset = temp_hdr.frame_body_start ? temp_hdr.frame_body_start : hdr_sz;
      _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Parsed main header from buffer %s. Sequence starts at offset: %" PRId64 "\n", buffer_name, info_ptr->sequence_offset );
    } else {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Main header not yet complete in buffer %s.\n", buffer_name );
      return false;
    }
  }
  
  if ( info_ptr->sequence_offset == 0 ) {
    return false; // Header not ready yet
  }
  
  // Simple linear frame parsing over the appended region
  vol_geom_size_t parse_pos = info_ptr->sequence_offset;
  uint32_t new_frames_found = 0;
  const vol_geom_size_t min_header_bytes = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);
  
  // Continue parsing from where we left off
  if ( *frame_count > 0 ) {
    vol_geom_buffer_frame_info_t* last_frame = &frame_directory[*frame_count - 1];
    parse_pos = last_frame->buffer_offset + last_frame->frame_size;
  }
  
  while ( parse_pos + min_header_bytes <= buffer_data_size && 
          *frame_count < buffer_state->max_frames_per_buffer ) {
    
    // Parse frame header at current position
    vol_geom_frame_hdr_t frame_header;
    vol_geom_size_t header_size;
    
    if ( !vol_geom_parse_frame_header_from_buffer( buffer_to_parse, parse_pos, &frame_header, &header_size ) ) {
      break; // Invalid header or need more data
    }
    
    // Set keyframe_number (matches logic from _build_frame_directory_from_file)
    if ( frame_header.keyframe == 1 || frame_header.keyframe == 2 ) {
      frame_header.keyframe_number = frame_header.frame_number;
    } else {
      // For non-keyframes, use the keyframe number from the previous frame
      uint32_t prev = (frame_header.frame_number > 0) ? (frame_header.frame_number - 1) : 0;
      if ( frame_header.frame_number > 0 && info_ptr->frame_headers_ptr ) {
        // Look up the previous frame's keyframe_number from the standard array
        frame_header.keyframe_number = info_ptr->frame_headers_ptr[prev].keyframe_number;
      } else {
        frame_header.keyframe_number = 0; // First frame case
      }
    }
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Parsed frame header: frame=%u, mesh_size=%u, keyframe_type=%u, keyframe_number=%u\n", 
      frame_header.frame_number, frame_header.mesh_data_sz, frame_header.keyframe, frame_header.keyframe_number );
    
    // Calculate total frame size
    vol_geom_size_t total_frame_size = header_size + frame_header.mesh_data_sz + sizeof(uint32_t);
    
    // Check if complete frame is available
    if ( parse_pos + total_frame_size > buffer_data_size ) {
      break; // Incomplete frame
    }

    // Validate trailing size sentinel matches mesh_data_sz to avoid drift
    uint32_t trailing_sz = 0;
    memcpy( &trailing_sz, buffer_to_parse + parse_pos + header_size + frame_header.mesh_data_sz, sizeof(uint32_t) );
    if ( trailing_sz != frame_header.mesh_data_sz ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR,
        "ERROR: TAIL_MISMATCH at frame=%u: mesh_sz=%u tail=%u at off=%" PRId64 " hdr=%" PRId64 " parse_pos=%" PRId64 "\n",
        frame_header.frame_number, frame_header.mesh_data_sz, trailing_sz,
        parse_pos + header_size + frame_header.mesh_data_sz, header_size, parse_pos );
      break;
    }
    
    // Add frame to streaming buffer directory
    vol_geom_buffer_frame_info_t* frame_info = &frame_directory[*frame_count];
    frame_info->frame_number = frame_header.frame_number;
    frame_info->buffer_offset = parse_pos;
    frame_info->frame_size = total_frame_size;
    frame_info->mesh_data_size = frame_header.mesh_data_sz;
    frame_info->header_size = header_size;
    
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "FRAME_SIZE_DEBUG: frame[%u] at idx[%u]: offset=%" PRId64 ", size=%" PRId64 ", mesh_size=%u, header_size=%" PRId64 "\n",
      frame_header.frame_number, *frame_count, parse_pos, total_frame_size, frame_header.mesh_data_sz, header_size );
    
    // Also update standard directory if arrays exist (for _read_vol_frame compatibility)
    if ( info_ptr->frames_directory_ptr && frame_header.frame_number < info_ptr->hdr.frame_count ) {
      info_ptr->frames_directory_ptr[frame_header.frame_number].hdr_sz = header_size;
      info_ptr->frames_directory_ptr[frame_header.frame_number].corrected_payload_sz = frame_header.mesh_data_sz;
      info_ptr->frames_directory_ptr[frame_header.frame_number].total_sz = total_frame_size;
      info_ptr->frames_directory_ptr[frame_header.frame_number].offset_sz = 0; // Not used in streaming
      
      // Store frame header too
      if ( info_ptr->frame_headers_ptr ) {
        info_ptr->frame_headers_ptr[frame_header.frame_number] = frame_header;
      }
      
      // Track biggest frame for blob allocation
      if ( total_frame_size > info_ptr->biggest_frame_blob_sz ) {
        info_ptr->biggest_frame_blob_sz = total_frame_size;
      }
    }
    
    (*frame_count)++;
    new_frames_found++;
    parse_pos += total_frame_size;
    
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Parsed frame %u at offset %" PRId64 " (size %" PRId64 ")\n",
      frame_header.frame_number, frame_info->buffer_offset, frame_info->frame_size );
  }
  
  if ( new_frames_found > 0 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Found %u new frames in %s buffer (total: %u frames)\n", 
      new_frames_found, buffer_name, *frame_count );
  }
  
  return new_frames_found > 0;
}

bool vol_geom_read_frame_streaming( vol_geom_info_t* info_ptr, uint32_t frame_idx, vol_geom_frame_data_t* frame_data_ptr ) {
  if ( !info_ptr || !frame_data_ptr || !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_read_frame_streaming() - invalid parameters.\n" );
    return false;
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  vol_geom_buffer_frame_info_t* frame_info = NULL;
  uint8_t* source_buffer = NULL;
  
  // Find the frame in the ring directory
  for ( uint32_t i = 0; i < buffer_state->frame_count; i++ ) {
    if ( buffer_state->frames[i].frame_number == frame_idx ) {
      frame_info = &buffer_state->frames[i];
      source_buffer = buffer_state->ring_buffer;
      break;
    }
  }
  
  if ( !frame_info ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Frame %u not found in any buffer.\n", frame_idx );
    return false;
  }
  
  // Simple linear copy from buffer to preallocated frame blob - no wraparound complexity!
  memcpy( info_ptr->preallocated_frame_blob_ptr, 
          source_buffer + frame_info->buffer_offset, 
          frame_info->frame_size );
  
  // Use existing frame parsing logic to process the copied data
  if ( !_read_vol_frame( info_ptr, frame_idx, frame_data_ptr ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Failed to parse frame %u from dual buffer.\n", frame_idx );
    return false;
  }
  
  // Update keyframe tracking (same as regular read_frame)
  if ( frame_idx < info_ptr->hdr.frame_count && info_ptr->frame_headers_ptr && 
       info_ptr->frame_headers_ptr[frame_idx].keyframe == 1 ) {
    info_ptr->last_keyframe = frame_idx;
  }
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Successfully read frame %u from ring buffer.\n", frame_idx );
  
  return true;
}

//
// ===== DUAL BUFFER MANAGEMENT FUNCTIONS =====
// Core functions for managing the dual buffer streaming system
//

bool vol_geom_is_download_buffer_full( const vol_geom_info_t* info_ptr ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_is_download_buffer_full() - invalid parameters.\n" );
    return false;
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  vol_geom_size_t full_threshold = (buffer_state->ring_capacity * 9) / 10; // 90%
  bool is_full = buffer_state->data_size >= full_threshold;
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Ring buffer: %.1f%% full (%"PRId64"/%"PRId64" bytes)\n",
    (float)buffer_state->data_size / (float)buffer_state->ring_capacity * 100.0f,
    buffer_state->data_size, buffer_state->ring_capacity );
  return is_full;
}

// Helper function to find the boundary of the last complete frame in a buffer
vol_geom_size_t vol_geom_find_last_complete_frame_boundary( vol_geom_buffer_frame_info_t* frame_directory, uint32_t frame_count ) {
  if ( frame_count == 0 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "find_last_complete_frame_boundary: No frames, returning 0\n" );
    return 0; // No frames, boundary is at start
  }
  
  // Get the last frame info
  vol_geom_buffer_frame_info_t* last_frame = &frame_directory[frame_count - 1];
  vol_geom_size_t boundary = last_frame->buffer_offset + last_frame->frame_size;
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "BOUNDARY_DEBUG: frame_count=%u, accessing idx[%u]: frame_num=%u, offset=%zu, size=%zu, mesh_size=%zu, header_size=%zu\n", 
    frame_count, frame_count-1, last_frame->frame_number, last_frame->buffer_offset, last_frame->frame_size, 
    last_frame->mesh_data_size, last_frame->header_size );
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "BOUNDARY_DEBUG: calculated boundary = %zu + %zu = %zu\n", 
    last_frame->buffer_offset, last_frame->frame_size, boundary );
  
  // Return the end of the last complete frame
  return boundary;
}

bool vol_geom_swap_buffers( vol_geom_info_t* info_ptr ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_swap_buffers() - invalid parameters.\n" );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  // In ring mode we compact (evict) frames strictly before the current playback frame
  if ( buffer_state->frame_count == 0 ) { return false; }

  // Determine the earliest frame we must keep: the keyframe for the current playback frame.
  uint32_t keep_from_frame = 0;
  if ( info_ptr->frame_headers_ptr && buffer_state->last_playback_frame < info_ptr->hdr.frame_count ) {
    uint32_t lpf = buffer_state->last_playback_frame;
    int32_t kf   = info_ptr->frame_headers_ptr[lpf].keyframe_number;
    if ( kf >= 0 ) { keep_from_frame = (uint32_t)kf; }
  }
  // If we don't know the keyframe yet, conservatively keep everything.
  if ( keep_from_frame == 0 ) { return false; }

  // Find the last frame strictly before the keyframe we need to keep
  int32_t boundary_index = -1;
  for ( uint32_t i = 0; i < buffer_state->frame_count; i++ ) {
    if ( buffer_state->frames[i].frame_number < keep_from_frame ) {
      boundary_index = (int32_t)i; // keep moving forward
    } else {
      break;
    }
  }
  if ( boundary_index < 0 ) { return false; } // nothing safe to evict yet

  vol_geom_size_t boundary = buffer_state->frames[boundary_index].buffer_offset + buffer_state->frames[boundary_index].frame_size;
  if (boundary <= 0 || boundary > buffer_state->data_size) { return false; }

  vol_geom_size_t bytes_to_keep = buffer_state->data_size - boundary;
  memmove( buffer_state->ring_buffer, buffer_state->ring_buffer + boundary, (size_t)bytes_to_keep );
  buffer_state->data_size = bytes_to_keep;

  // Rebuild frame directory keeping only frames at or after the boundary,
  // and rebase their offsets relative to the new start of the ring (offset 0).
  uint32_t new_count = 0;
  uint32_t first_kept = 0, last_kept = 0;
  bool first_set = false;
  for ( uint32_t i = 0; i < buffer_state->frame_count; i++ ) {
    const vol_geom_buffer_frame_info_t oldf = buffer_state->frames[i];
    if ( oldf.buffer_offset >= boundary ) {
      vol_geom_buffer_frame_info_t f = oldf;
      f.buffer_offset -= boundary;
      buffer_state->frames[new_count++] = f;
      if ( !first_set ) { first_kept = f.frame_number; first_set = true; }
      last_kept = f.frame_number;
    }
  }
  buffer_state->frame_count = new_count;

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "COMPACT_DEBUG: kept %u frames, first=%u last=%u, new_data_size=%" PRId64 "\n",
    buffer_state->frame_count, first_kept, last_kept, buffer_state->data_size );

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Ring compaction completed. data_size=%" PRId64 ", frames=%u\n",
    buffer_state->data_size, buffer_state->frame_count );

  return true;
}

const uint8_t* vol_geom_get_playback_buffer( const vol_geom_info_t* info_ptr, vol_geom_size_t* buffer_size ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr || !buffer_size ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_get_playback_buffer() - invalid parameters.\n" );
    return NULL;
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  *buffer_size = buffer_state->data_size;
  return buffer_state->ring_buffer;
}

bool vol_geom_create_streaming_file_info( vol_geom_info_t* info_ptr ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_create_streaming_file_info() - invalid parameters or no streaming buffer.\n" );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  
  // Try to parse header if it hasn't been parsed yet
  if ( info_ptr->sequence_offset == 0 || info_ptr->hdr.frame_count == 0 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Header not yet parsed, attempting to parse from buffer data...\n" );
    
    // Try to update buffer frame directory to parse header
    if ( !vol_geom_update_buffer_frame_directory( info_ptr ) ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Failed to update buffer frame directory.\n" );
      return false;
    }
    
    // Check again after trying to parse
    if ( info_ptr->sequence_offset == 0 || info_ptr->hdr.frame_count == 0 ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Still no header after parsing buffer. Need more data.\n" );
      return false;
    }
  }
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "Creating streaming file info from buffer data (%" PRIu32 " total frames).\n", info_ptr->hdr.frame_count );
  
  // Allocate standard frame headers array
  if ( !info_ptr->frame_headers_ptr ) {
    info_ptr->frame_headers_ptr = calloc( info_ptr->hdr.frame_count, sizeof(vol_geom_frame_hdr_t) );
    if ( !info_ptr->frame_headers_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating frame headers array.\n" );
      return false;
    }
  }
  
  // Allocate standard frame directory
  if ( !info_ptr->frames_directory_ptr ) {
    info_ptr->frames_directory_ptr = calloc( info_ptr->hdr.frame_count, sizeof(vol_geom_frame_directory_entry_t) );
    if ( !info_ptr->frames_directory_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating frame directory.\n" );
      free( info_ptr->frame_headers_ptr );
      info_ptr->frame_headers_ptr = NULL;
      return false;
    }
  }

  // Now that arrays exist, refresh the unified directory to populate them
  (void)vol_geom_update_buffer_frame_directory( info_ptr );
  
  // Frames currently parsed in the ring
  uint32_t frames_available = buffer_state->frame_count;
  
  // Allocate preallocated frame blob for frame reading (if not already done)
  if ( !info_ptr->preallocated_frame_blob_ptr && info_ptr->biggest_frame_blob_sz > 0 ) {
    info_ptr->preallocated_frame_blob_ptr = malloc( info_ptr->biggest_frame_blob_sz );
    if ( !info_ptr->preallocated_frame_blob_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating preallocated frame blob (%" PRId64 " bytes).\n", info_ptr->biggest_frame_blob_sz );
      return false;
    }
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Allocated frame blob: %" PRId64 " bytes\n", info_ptr->biggest_frame_blob_sz );
  } else if ( !info_ptr->preallocated_frame_blob_ptr ) {
    // Default size if no frames parsed yet
    info_ptr->biggest_frame_blob_sz = 10 * 1024 * 1024; // 10MB default
    info_ptr->preallocated_frame_blob_ptr = malloc( info_ptr->biggest_frame_blob_sz );
    if ( !info_ptr->preallocated_frame_blob_ptr ) {
      _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating default frame blob (%" PRId64 " bytes).\n", info_ptr->biggest_frame_blob_sz );
      return false;
    }
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Allocated default frame blob: %" PRId64 " bytes\n", info_ptr->biggest_frame_blob_sz );
  }
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "✅ Streaming file info created successfully. Frames available: %u/%u\n", 
    frames_available, info_ptr->hdr.frame_count );
  
  return frames_available > 0; // Success if we have at least some frames
}


