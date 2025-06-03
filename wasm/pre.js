// WASMFS/OPFS Initialization Logic

// Flag to track OPFS status
Module.opfsReady = false;
Module.opfsError = null;
Module.opfsMount = null; // Will store the mount promise

// Flag and promise for FS initialization
let resolveFS;
Module.fsInitialized = new Promise(resolve => {
  resolveFS = resolve;
});

async function initializeOPFS() {
  console.log("Attempting to initialize OPFS...");
  
  // Wait for FS to be ready
  await Module.fsInitialized;
  
  if (typeof navigator === 'undefined' || !navigator.storage || !navigator.storage.getDirectory) {
    console.warn("OPFS API not available in this environment.");
    Module.opfsError = "OPFS API not available";
    return false;
  }

  try {
    const opfsRoot = await navigator.storage.getDirectory();
    console.log("OPFS root directory obtained.");
    
    // Create the mount point if needed
    if (!Module.FS.analyzePath('/opfs').exists) {
      Module.FS.mkdir('/opfs');
    }
    
    // Create a proper promise for the mount operation
    Module.opfsMount = new Promise((resolve, reject) => {
      try {
        Module.FS.mount(OPFS, { root: '.' }, '/opfs');
        // Verify mount worked
        Module.FS.readdir('/opfs');
        Module.FS.chdir('/opfs');
        Module.opfsReady = true;
        resolve(true);
      } catch (err) {
        Module.opfsError = err.message;
        reject(err);
      }
    });

    await Module.opfsMount;
    console.log("OPFS successfully mounted at /opfs");
    return true;

  } catch (err) {
    console.error("Failed to initialize or mount OPFS:", err);
    Module.opfsError = err.message || "Unknown OPFS initialization error";
    Module.opfsReady = false;
    return false;
  }
}

// Function to run OPFS setup when runtime is initialized
async function setupOPFSOnInit() {
	await initializeOPFS().catch(err => {
    console.error("Unhandled error during OPFS initialization:", err);
    Module.opfsReady = false;
    Module.opfsError = Module.opfsError || "Error in onRuntimeInitialized wrapper";
  });
}

// Hook into Emscripten's runtime initialization callback
if (!Module.onRuntimeInitialized) {
  Module.onRuntimeInitialized = async function() {
    // Signal that FS is ready
    resolveFS();
    await setupOPFSOnInit();
  };
} else {
  // Chain if a callback already exists
  const oldOnRuntimeInitialized = Module.onRuntimeInitialized;
  Module.onRuntimeInitialized = function() {
    oldOnRuntimeInitialized.apply(null, arguments);
    resolveFS();
    setupOPFSOnInit();
  };
}

// End of WASMFS/OPFS Initialization Logic


// New OPFS Fetch Functions

/**
 * Fetches a file using streaming and stores it directly into OPFS.
 * @param {string} destFilename The name of the file to create/overwrite in OPFS.
 * @param {string} fileUrl The URL to fetch the file from.
 * @param {function(number):void} [onProgress] Optional callback for download progress (0.0 to 1.0).
 * @returns {Promise<{status: number, fileHandle: FileSystemFileHandle}>} A promise resolving with fetch status and the OPFS file handle.
 */
Module.fetch_stream_file_opfs = async (destFilename, fileUrl, onProgress) => {
  console.log(`Attempting to fetch stream to OPFS: ${destFilename} from ${fileUrl}`);
  
  // Wait for OPFS mount to complete
  if (!Module.opfsReady) {
    try {
      await Module.opfsMount;
    } catch (err) {
      throw new Error(`OPFS is not available or failed to initialize. Error: ${Module.opfsError}`);
    }
  }

  try {
    // Get the OPFS root directory handle
    const root = await navigator.storage.getDirectory();
	console.log(root)
    
    // Get a handle to the target file in OPFS, creating it if it doesn't exist
    const fileHandle = await root.getFileHandle(destFilename, { create: true });
	console.log(fileHandle)
    
    // Create a writable stream to the file
    const writable = await fileHandle.createWritable(); // Default keeps existing content, use { keepExistingData: false } to truncate
    
    console.log(`Fetching ${fileUrl}...`);
    const response = await fetch(fileUrl);
    
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status} for ${fileUrl}`);
    }
    
    if (!response.body) {
      throw new Error(`Response body is null for ${fileUrl}`);
    }
    
    const reader = response.body.getReader();
    const totalSize = Number(response.headers.get('content-length') || 0);
    let bytesWritten = 0;
    
    console.log(`Streaming response body to OPFS file: ${destFilename}`);
    
    // Stream the response body directly to the OPFS file
    while (true) {
      const { done, value } = await reader.read();
      
      if (done) {
        break;
      }
      
      if (value) {
        await writable.write(value);
        bytesWritten += value.byteLength;
        
        if (onProgress && totalSize > 0) {
          onProgress(bytesWritten / totalSize);
        }
      }
    }
    
    await writable.close();
    console.log(`File ${destFilename} successfully downloaded and stored in OPFS.`);
	console.log(Module.FS.stat('/'))
    
    if (onProgress) onProgress(1.0); // Ensure progress reaches 100%
    
    return { status: response.status, fileHandle: fileHandle };
  } catch (error) {
    console.error(`Error in fetch_stream_file_opfs for ${destFilename}:`, error);
    throw error; // Re-throw the error to be caught by the caller
  }
};

/**
 * Fetches a file (potentially non-streaming internally, but writes chunked) and stores it directly into OPFS.
 * Currently implemented identically to fetch_stream_file_opfs for efficiency with large files.
 * @param {string} destFilename The name of the file to create/overwrite in OPFS.
 * @param {string} fileUrl The URL to fetch the file from.
 * @param {function(number):void} [onProgress] Optional callback for download progress (0.0 to 1.0).
 * @returns {Promise<{status: number, fileHandle: FileSystemFileHandle}>} A promise resolving with fetch status and the OPFS file handle.
 */
Module.fetch_file_opfs = async (destFilename, fileUrl, onProgress) => {
  // For now, this function behaves identically to the streaming version,
  // as chunked writing to OPFS is generally preferred for potentially large files.
  return Module.fetch_stream_file_opfs(destFilename, fileUrl, onProgress);
};

// End of New OPFS Fetch Functions

Module.fileFetched = false;
Module.headerFetched = false;
Module.isHeaderLoaded = () => {
  console.log(Module.fileFetched);
  const poll = resolve => {
    if(Module.headerFetched == true) resolve();
    else setTimeout(_ => poll(resolve), 400);
  }
  return new Promise(poll);
}

Module.fetch_stream_file = async (dest, fileUrl, onProgress) => {
  // Wait for FS to be ready
  await Module.fsInitialized;
  
  // Add a warning if OPFS is ready but this MEMFS function is called
  if (Module.opfsReady && !dest.startsWith('/opfs/')) {
    console.warn("fetch_stream_file writes to MEMFS. Consider using fetch_stream_file_opfs for persistent storage.");
  }
  
  return await fetch(fileUrl)
    // Retrieve its body as ReadableStream
    .then(async (response) => {
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      
      const reader = response.body.getReader();
      var fileStream = Module.FS.open(dest, "w");
      var seekLocation = 0;
      var fileSize = response.headers.get("content-length");
      
      reader.read().then(function pump({ done, value }) {
        if (onProgress) {
          onProgress(seekLocation/fileSize);
        }
        
        if (done) {
          // Do something with last chunk of data then exit reader
          Module.FS.close(fileStream);
          Module.fileFetched = true;
          console.log(('Fetching stream finished.'));
          return;
        }
        
        // Otherwise do something here to process current chunk
        Module.FS.write(fileStream, value, 0, value.length, seekLocation);
        seekLocation += value.length
        
        // Read some more, and call this function again (1048576 ~ 1MB)
        if (seekLocation > 1048576) {
          Module.headerFetched = true;
        }
        
        return reader.read().then(pump);
      })
    })
    .then((url) => console.log(('Stream ready.')))
	// .then(() => console.log(Module.FS.getPath(dest)))
    .catch((err) => console.error(err));
};

Module.fetch_file = async (dest, fileUrl, onProgress) => {
  // Wait for FS to be ready
  await Module.fsInitialized;
  
  // Add a warning if OPFS is ready but this MEMFS function is called
  if (Module.opfsReady && !dest.startsWith('/opfs/')) {
    console.warn("fetch_file writes to MEMFS. Consider using fetch_file_opfs for persistent storage.");
  }
  
  return await fetch(fileUrl)
    // Retrieve its body as ReadableStream
    .then(async (response) => {
      if (!response.ok) {
        throw new Error(`HTTP error! Status: ${response.status}`);
      }
      
      const reader = response.body.getReader();
      var fileStream = Module.FS.open(dest, "w");
      var seekLocation = 0;
      var fileSize = response.headers.get("content-length");
      
      await reader.read().then(async function pump({ done, value }) {
        if (onProgress) {
          onProgress(seekLocation/fileSize);
        }
        
        if (done) {
          // Do something with last chunk of data then exit reader
          Module.FS.close(fileStream);
          Module.fileFetched = true;
          console.log(('Fetching stream finished.'));
          return;
        }
        
        // Otherwise do something here to process current chunk
        Module.FS.write(fileStream, value, 0, value.length, seekLocation);
        seekLocation += value.length
        
        // Read some more, and call this function again
        return await reader.read().then(pump);
      })
    })
    .then((url) => console.log(('Stream ready.')))
    .catch((err) => console.error(err));
};

Module.initVologramFunctions = (containerObject) => {
  var usingExternalObject = false;
  let insertObject = Module;
  if (containerObject) {
    usingExternalObject = true;
    insertObject = containerObject;
  }
  
  insertObject["has_normals"] = Module.cwrap("has_normals", "boolean");
  insertObject["has_texture"] = Module.cwrap("has_texture", "boolean");
  insertObject["has_audio"] = Module.cwrap("has_audio", "boolean");
  insertObject["texture_compression"] = Module.cwrap("texture_compression", "number");
  insertObject["texture_container_format"] = Module.cwrap("texture_container_format", "number");
  insertObject["texture_width"] = Module.cwrap("texture_width", "number");
  insertObject["texture_height"] = Module.cwrap("texture_height", "number");
  insertObject["create_file_info"] = Module.cwrap("create_file_info", "boolean", ["string", "string"]);
  insertObject["create_single_file_info"] = Module.cwrap("create_single_file_info", "boolean", ["string"]);
  insertObject["free_file_info"] = Module.cwrap("free_file_info", "boolean");
  insertObject["frame_count"] = Module.cwrap("frame_count", "number");
  insertObject["loaded_frame_number"] = Module.cwrap("loaded_frame_number", "number");
  insertObject["read_frame"] = Module.cwrap("read_frame", "boolean", ["number"]);
  insertObject["update_frames_directory"] = Module.cwrap("update_frames_directory", "boolean", ["number"]);
  insertObject["max_blob_sz"] = Module.cwrap("max_blob_sz", "number");
  insertObject["is_keyframe"] = Module.cwrap("is_keyframe", "boolean", ["number"]);
  insertObject["find_previous_keyframe"] = Module.cwrap("find_previous_keyframe", "number", ["number"]);
  insertObject["frame_vertices"] = Module.cwrap("frame_vertices", "array");
  insertObject["frame_vertices_sz"] = Module.cwrap("frame_vertices_sz", "number");
  insertObject["frame_uvs_sz"] = Module.cwrap("frame_uvs_sz", "number");
  insertObject["frame_normals_sz"] = Module.cwrap("frame_normals_sz", "number");
  insertObject["frame_texture_data_ptr"] = Module.cwrap("frame_texture_data_ptr", "number");
  insertObject["frame_texture_sz"] = Module.cwrap("frame_texture_sz", "number");
  insertObject["frame_indices"] = Module.cwrap("frame_i", "array");
  insertObject["frame_i_sz"] = Module.cwrap("frame_i_sz", "number");
  insertObject["frame_data_ptr"] = Module.cwrap("frame_data_ptr", "array");
  insertObject["frame_vp_offset"] = Module.cwrap("frame_vp_offset", "number");
  insertObject["frame_vp_copied"] = Module.cwrap("frame_vp_copied", "array");
  insertObject["frame_uvs_copied"] = Module.cwrap("frame_uvs_copied", "array");
  insertObject["frame_normals_copied"] = Module.cwrap("frame_normals_copied", "array");
  insertObject["frame_indices_copied"] = Module.cwrap("frame_indices_copied", "array");
  insertObject["basis_transcode"] = Module.cwrap("basis_transcode", "boolean", ["number", "number", "number"]);
  insertObject["basis_get_transcoded_ptr"] = Module.cwrap("basis_get_transcoded_ptr", "number");
  insertObject["basis_get_transcoded_sz"] = Module.cwrap("basis_get_transcoded_sz", "number");
  insertObject["run_basis_transcode"] = Module.cwrap("run_basis_transcode", "boolean", ["number"]);
  insertObject["audio_data_ptr"] = Module.cwrap("audio_data_ptr", "array");
  insertObject["audio_data_sz"] = Module.cwrap("audio_data_sz", "number");
  
  insertObject["frame_get_verts"] = () => {
    const vp_copied = Module.ccall("frame_vp_copied", "array");
    const vp_sz = Module.ccall("frame_vertices_sz", "number");
    return new Float32Array(Module.HEAP8.buffer, vp_copied, vp_sz / 4);
  };
  
  insertObject["frame_get_norms"] = () => {
    const normals_copied = Module.ccall("frame_normals_copied", "array");
    const normals_sz = Module.ccall("frame_normals_sz", "number");
    return new Float32Array(Module.HEAP8.buffer, normals_copied, normals_sz / 4);
  };
  
  insertObject["frame_get_uvs"] = () => {
    const uvs_copied = Module.ccall("frame_uvs_copied", "array");
    const uvs_sz = Module.ccall("frame_uvs_sz", "number");
    return new Float32Array(Module.HEAP8.buffer, uvs_copied, uvs_sz / 4);
  };
  
  insertObject["frame_get_ind"] = () => {
    const indices_copied = Module.ccall("frame_indices_copied", "array");
    const indices_sz = Module.ccall("frame_i_sz", "number");
    const n_indices = indices_sz / 2; // ushort
    return new Uint16Array(Module.HEAP8.buffer, indices_copied, n_indices);
  };
  
  insertObject["basis_get_data"] = () => {
    const ptr = Module.ccall("basis_get_transcoded_ptr", "number");
    const w = Module.ccall("texture_width", "number");
    const h = Module.ccall("texture_height", "number");
    return new Uint8Array(Module.HEAP8.buffer, ptr, w * h);
  };
  
  insertObject["find_basis_fmt"] = (gl, hasAlpha = true) => {
    // Matches enum values in basisu_transcoder.h.
    const BASIS_FORMATS = {
      ETC1: 0, // Not on iOS. 97% of Android.
      ETC2: 1, // 97% of Android.
      BC1: 2, // (DXT1) Not on mobile.
      BC3: 3, // (DXT5) Not on mobile.
      BC4: 4, // Not on mobile.
      BC5: 5, // Not on mobile.
      BC7: 6, // Not on iOS. Partial Android. Not on Windows, but on Linux on same GPU!
      PVRTC1_4_RGB: 8, // Only PowerVR (iOS and some Android).
      PVRTC1_4_RGBA: 9, // Only PowerVR (iOS and some Android). cTFPVRTC1_4_RGBA = 9
      ASTC_4x4: 10, // Mali ARM, Intel GPUs, Nvidia Tegra.
      ATC_RGB: 11, // ~half of Android devices
      ATC_RGBA_INTERPOLATED_ALPHA: 12, // ~half of Android devices
      RGBA32: 13,
      RGB565: 14,
      BGR565: 15,
      RGBA4444: 16,
    };
    
    const available_extensions = gl.getSupportedExtensions();
    var glFmt;
    var basisFmt;
    var format_text = "";
    
    // S3TC DXT (Desktop)
    if (
      // (Desktop 1) https://developer.mozilla.org/en-US/docs/Web/API/WEBGL_compressed_texture_s3tc
      available_extensions.indexOf("WEBGL_compressed_texture_s3tc") > -1 ||
      available_extensions.indexOf("MOZ_WEBGL_compressed_texture_s3tc") > -1 ||
      available_extensions.indexOf("WEBKIT_WEBGL_compressed_texture_s3tc") > -1
    ) {
      let dxt_ext =
        gl.getExtension("WEBGL_compressed_texture_s3tc") ||
        gl.getExtension("MOZ_WEBGL_compressed_texture_s3tc") ||
        gl.getExtension("WEBKIT_WEBGL_compressed_texture_s3tc");
      glFmt = hasAlpha ? dxt_ext.COMPRESSED_RGBA_S3TC_DXT5_EXT : dxt_ext.COMPRESSED_RGB_S3TC_DXT1_EXT;
      basisFmt = hasAlpha ? BASIS_FORMATS.BC3 : BASIS_FORMATS.BC1;
      format_text = hasAlpha ? "COMPRESSED_RGBA_S3TC_DXT5_EXT (BC3)" : "COMPRESSED_RGB_S3TC_DXT1_EXT (BC1)";
      
    // ASTC (iOS)
    } else if (available_extensions.indexOf("WEBGL_compressed_texture_astc") > -1) {
      // (iOS) https://developer.mozilla.org/en-US/docs/Web/API/WEBGL_compressed_texture_astc
      let astc_ext = gl.getExtension("WEBGL_compressed_texture_astc");
      glFmt = astc_ext.COMPRESSED_RGBA_ASTC_4x4_KHR;
      basisFmt = BASIS_FORMATS.ASTC_4x4;
      format_text = "COMPRESSED_RGBA_ASTC_4x4_KHR";
      
    // ETC (Android)
    } else if (available_extensions.indexOf("WEBGL_compressed_texture_etc") > -1) {
      // https://developer.mozilla.org/en-US/docs/Web/API/WEBGL_compressed_texture_etc1
      let etc_ext = gl.getExtension("WEBGL_compressed_texture_etc");
      glFmt = etc_ext.COMPRESSED_RGBA8_ETC2_EAC;
      basisFmt = BASIS_FORMATS.ETC2;
      format_text = "COMPRESSED_RGBA8_ETC2_EAC";
      
    // PVRTC V1 (Note V2 also available).
    } else if (
      // https://developer.mozilla.org/en-US/docs/Web/API/WEBGL_compressed_texture_pvrtc
      available_extensions.indexOf("WEBGL_compressed_texture_pvrtc") > -1 ||
      available_extensions.indexOf("WEBKIT_WEBGL_compressed_texture_pvrtc") > -1
    ) {
      let pvrtc_ext =
        gl.getExtension("WEBGL_compressed_texture_pvrtc") || gl.getExtension("WEBKIT_WEBGL_compressed_texture_pvrtc");
      glFmt = hasAlpha ? pvrtc_ext.COMPRESSED_RGBA_PVRTC_4BPPV1_IMG : pvrtc_ext.COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
      basisFmt = hasAlpha ? BASIS_FORMATS.PVRTC1_4_RGBA : BASIS_FORMATS.PVRTC1_4_RGB;
      format_text = hasAlpha ? "COMPRESSED_RGBA_PVRTC_4BPPV1_IMG" : "COMPRESSED_RGB_PVRTC_4BPPV1_IMG";
      
    // MS BC7 (non-Windows desktops)
    } else if (available_extensions.indexOf("EXT_texture_compression_bptc") > -1) {
      // (Desktop 2) https://developer.mozilla.org/en-US/docs/Web/API/EXT_texture_compression_bptc
      let bc7_ext = gl.getExtension("EXT_texture_compression_bptc");
      glFmt = bc7_ext.COMPRESSED_RGBA_BPTC_UNORM_EXT;
      basisFmt = BASIS_FORMATS.BC7;
      format_text = "COMPRESSED_RGBA_BPTC_UNORM_EXT (BC7)";
    }
    
    //console.log("Using basis format " + basisFmt + " (" + format_text + ")");
    return [glFmt, basisFmt];
  };
  
  insertObject["get_audio_data"] = () => {
    let ptr = Module.ccall("audio_data_ptr", "array");
    let sz = Module.ccall("audio_data_sz", "number");
    return new Uint8Array(Module.HEAP8.buffer, ptr, sz);
  };
  
  if (usingExternalObject) {
    insertObject.HEAP8 = Module.HEAP8;
  }
};
