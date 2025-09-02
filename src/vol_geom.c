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
    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Freeing streaming buffer memory.\n" );
    if ( info_ptr->streaming_buffer_ptr->buffer_ptr ) {
      free( info_ptr->streaming_buffer_ptr->buffer_ptr );
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
  if ( frame_idx >= info_ptr->hdr.frame_count ) { return -1; }
  if ( info_ptr->frame_headers_ptr[frame_idx].mesh_data_sz > 0 ) { 
    // _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "INFO: find_previous_keyframe from keyframe_number is %i .\n", info_ptr->frame_headers_ptr[frame_idx].keyframe_number );
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
  const vol_geom_size_t frame_header_size = sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint8_t);
  *header_size_ptr = frame_header_size;

  // Parse the frame header from the buffer at the specified offset
  const uint8_t* read_ptr = buffer_ptr + offset;

  // Read frame_number (4 bytes, little-endian)
  header_ptr->frame_number = *((uint32_t*)read_ptr);
  read_ptr += sizeof(uint32_t);

  // Read mesh_data_sz (4 bytes, little-endian) 
  header_ptr->mesh_data_sz = *((uint32_t*)read_ptr);
  read_ptr += sizeof(uint32_t);

  // Read keyframe_number (4 bytes, little-endian)
  header_ptr->keyframe_number = *((uint32_t*)read_ptr);  
  read_ptr += sizeof(uint32_t);

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

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Parsed frame header: frame=%u, mesh_size=%u, keyframe_num=%u, keyframe_type=%u\n",
    header_ptr->frame_number, header_ptr->mesh_data_sz, header_ptr->keyframe_number, header_ptr->keyframe );

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
  buffer_state->buffer_size = config_ptr->max_buffer_size;

  // Allocate the circular buffer memory
  buffer_state->buffer_ptr = calloc( 1, buffer_state->buffer_size );
  if ( !buffer_state->buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: OOM allocating circular buffer of %.1fMB.\n",
      buffer_state->buffer_size / (1024.0 * 1024.0) );
    free( info_ptr->streaming_buffer_ptr );
    info_ptr->streaming_buffer_ptr = NULL;
    return false;
  }

  // Initialize buffer state
  buffer_state->write_pos = 0;
  buffer_state->valid_start = 0;
  buffer_state->valid_end = 0;
  buffer_state->file_pos = 0;
  buffer_state->file_size = 0; // Will be set when known
  buffer_state->is_streaming_mode = true;
  buffer_state->frames_in_buffer = 0;
  buffer_state->first_frame_in_buffer = 0;
  buffer_state->last_frame_in_buffer = 0;

  _vol_loggerf( VOL_GEOM_LOG_TYPE_INFO, "Created streaming buffer: %.1fMB capacity, %.1fMB reserved for first frame.\n",
    buffer_state->buffer_size / (1024.0 * 1024.0), 
    config_ptr->reserved_space_size / (1024.0 * 1024.0) );

  return true;
}

bool vol_geom_add_data_to_buffer( vol_geom_info_t* info_ptr, const uint8_t* data_ptr, vol_geom_size_t data_size ) {
  if ( !info_ptr || !data_ptr || data_size <= 0 ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_add_data_to_buffer() - invalid parameters.\n" );
    return false;
  }

  if ( !info_ptr->streaming_buffer_ptr || !info_ptr->streaming_buffer_ptr->buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: streaming buffer not initialized.\n" );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  // Check if we have enough space in the buffer
  vol_geom_size_t available_space = buffer_state->buffer_size - (buffer_state->valid_end - buffer_state->valid_start);
  if ( data_size > available_space ) {
    // Calculate how much space we need to free up
    vol_geom_size_t space_needed = data_size - available_space;
    
    // For now, we'll advance the valid_start to make space
    // In a more sophisticated implementation, we'd check frame boundaries
    buffer_state->valid_start += space_needed;
    if ( buffer_state->valid_start >= buffer_state->buffer_size ) {
      buffer_state->valid_start %= buffer_state->buffer_size;
    }

    _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Made space in buffer by advancing valid_start by %" PRId64 " bytes.\n", space_needed );
  }

  // Calculate where to write the data (with wraparound)
  vol_geom_size_t write_pos = buffer_state->write_pos % buffer_state->buffer_size;
  vol_geom_size_t bytes_remaining = data_size;
  const uint8_t* src_ptr = data_ptr;

  // Handle potential wraparound when writing
  while ( bytes_remaining > 0 ) {
    vol_geom_size_t bytes_to_end = buffer_state->buffer_size - write_pos;
    vol_geom_size_t bytes_to_copy = (bytes_remaining < bytes_to_end) ? bytes_remaining : bytes_to_end;

    // Copy data to buffer
    memcpy( buffer_state->buffer_ptr + write_pos, src_ptr, bytes_to_copy );

    // Update positions
    write_pos = (write_pos + bytes_to_copy) % buffer_state->buffer_size;
    src_ptr += bytes_to_copy;
    bytes_remaining -= bytes_to_copy;
  }

  // Update buffer state
  buffer_state->write_pos = write_pos;
  buffer_state->valid_end += data_size;
  buffer_state->file_pos += data_size;

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Added %" PRId64 " bytes to buffer. Valid range: %" PRId64 "-%" PRId64 ", write_pos: %" PRId64 ", file_pos: %" PRId64 "\n",
    data_size, buffer_state->valid_start, buffer_state->valid_end, buffer_state->write_pos, buffer_state->file_pos );

  return true;
}

bool vol_geom_is_frame_available_in_buffer( const vol_geom_info_t* info_ptr, uint32_t frame_idx ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    return false; // Not in streaming mode or buffer not initialized
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  // Check if frame is within the available range in the buffer
  if ( buffer_state->frames_in_buffer == 0 ) {
    return false; // No frames in buffer yet
  }

  // Check if frame is within the available frame range
  return (frame_idx >= buffer_state->first_frame_in_buffer && frame_idx <= buffer_state->last_frame_in_buffer);
}

vol_geom_size_t vol_geom_get_buffer_health_bytes( const vol_geom_info_t* info_ptr ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    return 0; // Not in streaming mode or buffer not initialized
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  // Calculate valid data size in buffer
  vol_geom_size_t valid_bytes = buffer_state->valid_end - buffer_state->valid_start;

  return valid_bytes;
}

float vol_geom_get_buffer_health_seconds( const vol_geom_info_t* info_ptr, float fps ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr || fps <= 0.0f ) {
    return 0.0f;
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  if ( buffer_state->frames_in_buffer == 0 ) {
    return 0.0f; // No frames available
  }

  // Estimate seconds based on available frames
  float seconds = (float)buffer_state->frames_in_buffer / fps;

  return seconds;
}

bool vol_geom_should_resume_download( const vol_geom_info_t* info_ptr, uint32_t current_frame, float fps ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr || fps <= 0.0f ) {
    return true; // Default to resume if not in streaming mode
  }

  const vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;

  // Calculate buffer health in seconds
  float buffer_health_seconds = vol_geom_get_buffer_health_seconds( info_ptr, fps );

  // Resume download if buffer health is below the lookahead threshold
  bool should_resume = buffer_health_seconds < buffer_state->config.lookahead_seconds;

  // Also check if current frame is getting close to the end of buffered content
  if ( buffer_state->frames_in_buffer > 0 ) {
    uint32_t frames_ahead = buffer_state->last_frame_in_buffer - current_frame;
    float seconds_ahead = (float)frames_ahead / fps;
    
    if ( seconds_ahead < buffer_state->config.lookahead_seconds ) {
      should_resume = true;
    }
  }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Buffer health: %.1fs, lookahead: %.1fs, current_frame: %u, last_buffered: %u -> %s download\n",
    buffer_health_seconds, buffer_state->config.lookahead_seconds, current_frame, 
    buffer_state->last_frame_in_buffer, should_resume ? "resume" : "pause" );

  return should_resume;
}

bool vol_geom_update_buffer_frame_directory( vol_geom_info_t* info_ptr ) {
  if ( !info_ptr || !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_update_buffer_frame_directory() - invalid parameters.\n" );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  
  // Start scanning from the last known position in the buffer
  vol_geom_size_t scan_pos = buffer_state->valid_start;
  uint32_t frames_found = 0;
  uint32_t first_frame_num = 0;
  uint32_t last_frame_num = 0;
  
  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Scanning buffer for frames. Valid range: %" PRId64 "-%" PRId64 "\n",
    buffer_state->valid_start, buffer_state->valid_end );

  // Scan the valid data range for complete frames
  while ( scan_pos < buffer_state->valid_end ) {
    vol_geom_size_t bytes_remaining = buffer_state->valid_end - scan_pos;
    
    // Need at least a frame header to continue
    vol_geom_frame_hdr_t frame_header;
    vol_geom_size_t header_size;
    
    if ( bytes_remaining < sizeof(vol_geom_frame_hdr_t) ) {
      break; // Not enough data for a complete header
    }

    // Parse frame header from buffer (handling circular buffer wraparound)
    vol_geom_size_t buffer_offset = scan_pos % buffer_state->buffer_size;
    
    // Handle the case where header spans the wraparound boundary
    if ( buffer_offset + sizeof(vol_geom_frame_hdr_t) > buffer_state->buffer_size ) {
      // Header spans boundary - copy to temporary buffer for parsing
      uint8_t temp_header[sizeof(vol_geom_frame_hdr_t)];
      vol_geom_size_t first_part = buffer_state->buffer_size - buffer_offset;
      vol_geom_size_t second_part = sizeof(vol_geom_frame_hdr_t) - first_part;
      
      memcpy( temp_header, buffer_state->buffer_ptr + buffer_offset, first_part );
      memcpy( temp_header + first_part, buffer_state->buffer_ptr, second_part );
      
      if ( !vol_geom_parse_frame_header_from_buffer( temp_header, 0, &frame_header, &header_size ) ) {
        break; // Invalid frame header
      }
    } else {
      // Header fits within buffer boundaries
      if ( !vol_geom_parse_frame_header_from_buffer( buffer_state->buffer_ptr, buffer_offset, &frame_header, &header_size ) ) {
        break; // Invalid frame header
      }
    }

    // Calculate total frame size (header + mesh data + trailing size int)
    vol_geom_size_t total_frame_size = header_size + frame_header.mesh_data_sz + sizeof(uint32_t);
    
    // Check if we have the complete frame in the buffer
    if ( bytes_remaining < total_frame_size ) {
      break; // Incomplete frame - wait for more data
    }

    // We have a complete frame! Update the frame directory
    uint32_t frame_idx = frame_header.frame_number;
    
    if ( frame_idx < info_ptr->hdr.frame_count ) {
      // Update frame directory entry (convert buffer position to file position)
      info_ptr->frames_directory_ptr[frame_idx].offset_sz = scan_pos;  // Store buffer position for now
      info_ptr->frames_directory_ptr[frame_idx].total_sz = total_frame_size;
      info_ptr->frames_directory_ptr[frame_idx].hdr_sz = header_size;
      info_ptr->frames_directory_ptr[frame_idx].corrected_payload_sz = frame_header.mesh_data_sz + sizeof(uint32_t);
      
      // Update frame header
      info_ptr->frame_headers_ptr[frame_idx] = frame_header;
      
      // Track frame range
      if ( frames_found == 0 ) {
        first_frame_num = frame_idx;
      }
      last_frame_num = frame_idx;
      frames_found++;
      
      _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Found complete frame %u at buffer pos %" PRId64 ", size %" PRId64 "\n",
        frame_idx, scan_pos, total_frame_size );
    }

    // Move to next potential frame
    scan_pos += total_frame_size;
  }

  // Update buffer state
  buffer_state->frames_in_buffer = frames_found;
  if ( frames_found > 0 ) {
    buffer_state->first_frame_in_buffer = first_frame_num;
    buffer_state->last_frame_in_buffer = last_frame_num;
  }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Buffer directory updated: %u frames found (range %u-%u)\n",
    frames_found, first_frame_num, last_frame_num );

  return true;
}

bool vol_geom_read_frame_streaming( vol_geom_info_t* info_ptr, uint32_t frame_idx, vol_geom_frame_data_t* frame_data_ptr ) {
  if ( !info_ptr || !frame_data_ptr || !info_ptr->streaming_buffer_ptr ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: vol_geom_read_frame_streaming() - invalid parameters.\n" );
    return false;
  }

  // Check if frame is available in buffer
  if ( !vol_geom_is_frame_available_in_buffer( info_ptr, frame_idx ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Frame %u is not available in buffer.\n", frame_idx );
    return false;
  }

  vol_geom_buffer_state_t* buffer_state = info_ptr->streaming_buffer_ptr;
  
  // Get frame directory entry (which contains buffer positions)
  vol_geom_size_t buffer_offset = info_ptr->frames_directory_ptr[frame_idx].offset_sz;
  vol_geom_size_t total_size = info_ptr->frames_directory_ptr[frame_idx].total_sz;

  // Copy frame data from circular buffer to the preallocated frame blob
  vol_geom_size_t buffer_pos = buffer_offset % buffer_state->buffer_size;
  vol_geom_size_t bytes_remaining = total_size;
  uint8_t* dest_ptr = info_ptr->preallocated_frame_blob_ptr;

  // Handle potential wraparound when copying frame data
  while ( bytes_remaining > 0 ) {
    vol_geom_size_t bytes_to_end = buffer_state->buffer_size - buffer_pos;
    vol_geom_size_t bytes_to_copy = (bytes_remaining < bytes_to_end) ? bytes_remaining : bytes_to_end;

    memcpy( dest_ptr, buffer_state->buffer_ptr + buffer_pos, bytes_to_copy );

    dest_ptr += bytes_to_copy;
    buffer_pos = (buffer_pos + bytes_to_copy) % buffer_state->buffer_size;
    bytes_remaining -= bytes_to_copy;
  }

  // Now use the existing frame parsing logic to process the copied data
  if ( !_read_vol_frame( info_ptr, frame_idx, frame_data_ptr ) ) {
    _vol_loggerf( VOL_GEOM_LOG_TYPE_ERROR, "ERROR: Failed to parse frame %u from streaming buffer.\n", frame_idx );
    return false;
  }

  // Update keyframe tracking (same as regular read_frame)
  if ( info_ptr->frame_headers_ptr[frame_idx].keyframe == 1 ) {
    info_ptr->last_keyframe = frame_idx;
  }

  _vol_loggerf( VOL_GEOM_LOG_TYPE_DEBUG, "Successfully read frame %u from streaming buffer.\n", frame_idx );

  return true;
}
