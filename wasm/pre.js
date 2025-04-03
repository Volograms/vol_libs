// Initialize and mount OPFS
Module.initOPFS = async () => {
	try {
		// Check if OPFS is supported
		if (!navigator.storage || !navigator.storage.getDirectory) {
			console.warn('OPFS is not supported in this browser.');
			return false;
		}

		// Use backend API to create a mount to OPFS
		console.log('Initializing OPFS...');
		
		// Create directory for OPFS mounting
		try {
			Module.FS.mkdir('/opfs');
		} catch (error) {
			// Directory might already exist
			console.log('OPFS directory already exists or could not be created:', error);
		}

		// Wait for OPFS mounting to complete
		// This is a simpler approach that relies on the built-in WasmFS OPFS support
		const opfsRoot = await navigator.storage.getDirectory();
		
		// Mark OPFS as initialized
		Module._opfsInitialized = true;
		console.log('OPFS initialized successfully');
		return true;
	} catch (error) {
		console.error('Failed to initialize OPFS:', error);
		Module._opfsInitialized = false;
		return false;
	}
};

// Create or open a file in OPFS
Module.getOPFSFile = async (filename, create = true) => {
	if (!Module._opfsInitialized) {
		await Module.initOPFS();
	}
	
	try {
		const root = await navigator.storage.getDirectory();
		const fileHandle = await root.getFileHandle(filename, { create });
		return fileHandle;
	} catch (error) {
		console.error(`Error accessing OPFS file '${filename}':`, error);
		return null;
	}
};

// Check if a file exists in OPFS
Module.fileExistsInOPFS = async (filePath) => {
	try {
		// Try to get the file without creating it
		const fileHandle = await Module.getOPFSFile(filePath, false);
		return fileHandle !== null;
	} catch (error) {
		// File does not exist or could not be accessed
		return false;
	}
};

// Write a file to OPFS directly
Module.writeToOPFS = async (filename, data) => {
	try {
		const fileHandle = await Module.getOPFSFile(filename, true);
		const writable = await fileHandle.createWritable();
		await writable.write(data);
		await writable.close();
		return true;
	} catch (error) {
		console.error(`Error writing to OPFS file '${filename}':`, error);
		return false;
	}
};

// Update fetch_file to use OPFS directly when possible
Module.fetch_file = (dest, fileUrl, onProgress) => {
	console.log(`Starting fetch_file for ${dest} from ${fileUrl}`);
	return new Promise((resolve, reject) => {
		const xhr = new XMLHttpRequest();
		xhr.open("GET", fileUrl, true);
		xhr.responseType = "arraybuffer";
		xhr.onprogress = (e) => {
			if (onProgress) {
				onProgress(e.loaded / e.total);
			}
			// Log download progress periodically
			if (e.loaded % 1000000 < 100000) { // Log roughly every 1MB
				console.log(`Downloading ${dest}: ${Math.round(e.loaded/1024/1024)}MB / ${Math.round(e.total/1024/1024)}MB (${Math.round(e.loaded/e.total*100)}%)`);
			}
		};
		xhr.onload = async () => {
			if (!xhr.response) {
				console.error(`No response received for ${dest}`);
				reject(new Error("No response received"));
				return;
			}
			const byteArray = new Uint8Array(xhr.response);
			console.log(`Download complete for ${dest}. Size: ${byteArray.length} bytes`);

			// First, try to write the file to OPFS directly
			if (Module._opfsInitialized) {
				try {
					console.log(`Attempting to write ${dest} to OPFS...`);
					const success = await Module.writeToOPFS(dest, byteArray);
					if (success) {
						// Set headerFetched to true regardless of storage location
						Module.headerFetched = true;
						console.log(`Successfully wrote ${dest} to OPFS`);
						resolve({ status: xhr.status, responseUrl: xhr.responseURL });
						return;
					}
				} catch (error) {
					console.error(`Error writing to OPFS, falling back to MEMFS:`, error);
				}
			}
			
			// Fallback to MEMFS if OPFS writing failed
			try {
				console.log(`Writing ${dest} to MEMFS...`);
				var stream = Module.FS.open(dest, "w");
				Module.FS.write(stream, byteArray, 0, byteArray.length, 0);
				Module.FS.close(stream);
				// Set headerFetched to true regardless of storage location
				Module.headerFetched = true;
				console.log(`Successfully wrote ${dest} to MEMFS`);
				resolve({ status: xhr.status, responseUrl: xhr.responseURL });
			} catch (fallbackError) {
				console.error(`Failed to write file to MEMFS: ${fallbackError}`);
				reject(new Error(`Failed to write file: ${fallbackError}`));
			}
		};
		xhr.onerror = () => {
			console.error(`Download failed for ${dest}`);
			reject(new Error("Download failed"));
		}
		xhr.onabort = () => {
			console.warn(`Download aborted for ${dest}`);
			reject(new Error("Download aborted"));
		}
		xhr.send(null);
	});
};

// Update fetch_stream_file to use OPFS
Module.fetch_stream_file = async (dest, fileUrl, onProgress) => {
	console.log(`Starting fetch_stream_file for ${dest} from ${fileUrl}`);
	
	// Check if OPFS is initialized
	if (!Module._opfsInitialized) {
		try {
			console.log(`OPFS not initialized, initializing now...`);
			Module._opfsInitialized = await Module.initOPFS();
			console.log(`OPFS initialization: ${Module._opfsInitialized ? "successful" : "failed"}`);
		} catch (error) {
			console.warn('OPFS initialization failed, using MEMFS instead:', error);
		}
	}

	try {
		console.log(`Fetching ${dest} from ${fileUrl}...`);
		const response = await fetch(fileUrl);
		if (!response.ok) {
			console.error(`HTTP error ${response.status} when fetching ${dest}`);
			throw new Error(`HTTP error! Status: ${response.status}`);
		}
		
		const fileSize = response.headers.get("content-length");
		console.log(`File size for ${dest}: ${Math.round(fileSize/1024/1024)}MB`);
		const reader = response.body.getReader();

		// Create a file in OPFS if possible
		let opfsWritable = null;
		let usingOPFS = false;
		if (Module._opfsInitialized) {
			try {
				console.log(`Attempting to create OPFS writable for ${dest}...`);
				const fileHandle = await Module.getOPFSFile(dest, true);
				opfsWritable = await fileHandle.createWritable();
				usingOPFS = true;
				console.log(`Successfully created OPFS writable for ${dest}`);
			} catch (error) {
				console.error('Failed to create OPFS writable stream, falling back to MEMFS:', error);
			}
		}

		// If OPFS failed, use MEMFS
		let memfsStream = null;
		if (!usingOPFS) {
			try {
				console.log(`Creating MEMFS stream for ${dest}...`);
				memfsStream = Module.FS.open(dest, "w");
				console.log(`Successfully created MEMFS stream for ${dest}`);
			} catch (error) {
				console.error(`Failed to open MEMFS file: ${error}`);
				throw new Error(`Failed to open MEMFS file: ${error}`);
			}
		}

		let seekLocation = 0;
		let lastLoggedPercent = 0;
		
		// Process the stream
		while (true) {
			const { done, value } = await reader.read();
			
			if (onProgress) {
				onProgress(seekLocation/fileSize);
			}
			
			// Log progress every 5%
			const percent = Math.floor((seekLocation/fileSize) * 100);
			if (percent % 5 === 0 && percent !== lastLoggedPercent) {
				console.log(`Streaming ${dest}: ${Math.round(seekLocation/1024/1024)}MB / ${Math.round(fileSize/1024/1024)}MB (${percent}%)`);
				lastLoggedPercent = percent;
			}
			
			if (done) {
				if (usingOPFS && opfsWritable) {
					await opfsWritable.close();
					console.log(`Closed OPFS writable for ${dest}`);
				} else if (memfsStream) {
					Module.FS.close(memfsStream);
					console.log(`Closed MEMFS stream for ${dest}`);
				}
				Module.fileFetched = true;
				console.info(`Fetching stream finished for ${dest}. File stored in ${usingOPFS ? 'OPFS' : 'MEMFS'}`);
				break;
			}
			
			try {
				if (usingOPFS && opfsWritable) {
					// For OPFS we specify the position in the write options
					await opfsWritable.write({
						type: 'write',
						position: seekLocation,
						data: value
					});
				} else if (memfsStream) {
					// For MEMFS we specify the position as a parameter
					Module.FS.write(memfsStream, value, 0, value.length, seekLocation);
				}
				
				seekLocation += value.length;
				
				// Set headerFetched flag once we've read enough data (1MB)
				if (seekLocation > 1048576 && !Module.headerFetched) {
					console.log(`Setting headerFetched = true after reading 1MB of data for ${dest}`);
					Module.headerFetched = true;
				}
			} catch (error) {
				console.error(`Writing stream failed for ${dest} at ${seekLocation/1048576} MB:`, error);
				
				// Clean up resources
				if (usingOPFS && opfsWritable) {
					try {
						await opfsWritable.close();
					} catch (closeError) {
						console.error('Error closing OPFS writable:', closeError);
					}
				} else if (memfsStream) {
					try {
						Module.FS.close(memfsStream);
					} catch (closeError) {
						console.error('Error closing MEMFS stream:', closeError);
					}
				}
				
				throw error;
			}
		}
		
		return { success: true, path: dest, storage: usingOPFS ? 'opfs' : 'memfs' };
	} catch (error) {
		console.error(`Stream download failed for ${dest}:`, error);
		throw error;
	}
};

// Helper to check if a file exists and get its path in various storage locations
Module.resolvePath = async (filePath) => {
	console.log(`Resolving path for ${filePath}...`);
	// Check OPFS first
	if (Module._opfsInitialized) {
		try {
			const exists = await Module.fileExistsInOPFS(filePath);
			if (exists) {
				console.log(`File found in OPFS: ${filePath}`);
				return `/opfs/${filePath}`;
			} else {
				console.log(`File NOT found in OPFS: ${filePath}`);
			}
		} catch (error) {
			console.warn(`Error checking OPFS for ${filePath}:`, error);
		}
	}
	
	// Then check MEMFS
	try {
		Module.FS.stat(filePath);
		console.log(`File found in MEMFS: ${filePath}`);
		return filePath;
	} catch (error) {
		// File not found in MEMFS
		console.log(`File NOT found in MEMFS: ${filePath}`);
	}
	
	console.warn(`File not found in any storage: ${filePath}`);
	return filePath; // Return original path as fallback
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
			ETC2: 1, //  97% of Android.
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
			// (iOS) https://  developer.mozilla.org/en-US/docs/Web/API/WEBGL_compressed_texture_astc
			let astc_ext = gl.getExtension("WEBGL_compressed_texture_astc");
			glFmt = astc_ext.COMPRESSED_RGBA_ASTC_4x4_KHR;
			basisFmt = BASIS_FORMATS.ASTC_4x4;
			format_text = "COMPRESSED_RGBA_ASTC_4x4_KHR";

			// ETC (Android)
		} else if (available_extensions.indexOf("WEBGL_compressed_texture_etc") > -1) {
			// https://developer. mozilla.org/en-US/docs/Web/API/WEBGL_compressed_texture_etc1
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
			// (Desktop 2) https:// developer.mozilla.org/en-US/docs/Web/API/EXT_texture_compression_bptc
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

// Pre-run initialization
Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
	// We'll initialize OPFS when the module starts
	console.log("Module preRun - will initialize OPFS when ready");
});

// Helper to check if a header is loaded
Module.isHeaderLoaded = () => {
	console.log(`Header loaded status: ${Module.headerFetched}`);

	const poll = resolve => {
		if (Module.headerFetched === true) {
			resolve();
		} else {
			setTimeout(_ => poll(resolve), 400);
		}
	};

	return new Promise(poll);
};

// Helper function to create a file in OPFS from an existing file in MEMFS
Module.copyToOPFS = async (memfsPath, opfsPath) => {
	if (!Module._opfsInitialized) {
		try {
			Module._opfsInitialized = await Module.initOPFS();
		} catch (error) {
			console.error("Failed to initialize OPFS:", error);
			return false;
		}
	}
	
	try {
		// Read the file from MEMFS
		const data = Module.FS.readFile(memfsPath);
		
		// Create directories if needed
		const dirs = opfsPath.split('/').slice(0, -1);
		let currentPath = '/opfs';
		
		for (const dir of dirs) {
			if (!dir) continue;
			currentPath += `/${dir}`;
			try {
				Module.FS.mkdir(currentPath);
			} catch (error) {
				// Directory might already exist
			}
		}
		
		// Write the file to OPFS
		const fullOpfsPath = `/opfs/${opfsPath}`;
		Module.FS.writeFile(fullOpfsPath, data);
		console.log(`Copied ${memfsPath} to ${fullOpfsPath}`);
		return true;
	} catch (error) {
		console.error(`Failed to copy file to OPFS: ${error}`);
		return false;
	}
};

// Add enhanced debugging for reading frames
const originalReadFrame = Module.read_frame;
if (originalReadFrame) {
    Module.read_frame = function(frame_idx) {
        console.log(`Attempting to read frame ${frame_idx}`);
        const result = originalReadFrame(frame_idx);
        console.log(`Frame ${frame_idx} read result: ${result ? 'success' : 'failed'}`);
        return result;
    };
}

// Add a debug helper to check OPFS file contents
Module.readFromOPFS = async (filename) => {
	try {
		console.log(`Debug: Attempting to read file from OPFS: ${filename}`);
		
		// First check if OPFS is initialized
		if (!Module._opfsInitialized) {
			console.log(`Debug: OPFS not initialized, initializing now...`);
			Module._opfsInitialized = await Module.initOPFS();
		}
		
		// Get the file handle
		const fileHandle = await Module.getOPFSFile(filename, false); // Don't create if doesn't exist
		if (!fileHandle) {
			console.error(`Debug: File '${filename}' not found in OPFS`);
			return null;
		}
		
		// Get file as File object
		const file = await fileHandle.getFile();
		console.log(`Debug: Found file in OPFS: ${filename}, size: ${file.size} bytes`);
		
		// Read a small header chunk to verify content
		const headerChunk = await file.slice(0, Math.min(100, file.size)).arrayBuffer();
		const headerBytes = new Uint8Array(headerChunk);
		
		// Log the first few bytes as hex for debug purposes
		let hexString = '';
		for (let i = 0; i < Math.min(20, headerBytes.length); i++) {
			hexString += headerBytes[i].toString(16).padStart(2, '0') + ' ';
		}
		
		console.log(`Debug: First bytes of file: ${hexString}`);
		return {
			filename,
			size: file.size,
			headerBytes
		};
	} catch (error) {
		console.error(`Debug: Error reading from OPFS file '${filename}':`, error);
		return null;
	}
};

// Add a function to verify WASM can access the file
Module.verifyWasmFileAccess = async (filename) => {
	try {
		console.log(`Debug: Verifying WASM access to file: ${filename}`);
		
		// First, check if file exists in OPFS or MEMFS
		let filePath = filename;
		let existsInOPFS = false;
		
		if (Module._opfsInitialized) {
			existsInOPFS = await Module.fileExistsInOPFS(filename);
			if (existsInOPFS) {
				filePath = `/opfs/${filename}`;
				console.log(`Debug: File exists in OPFS at path: ${filePath}`);
			}
		}
		
		if (!existsInOPFS) {
			// Check MEMFS
			try {
				Module.FS.stat(filename);
				console.log(`Debug: File exists in MEMFS at path: ${filename}`);
			} catch (error) {
				console.error(`Debug: File not found in either OPFS or MEMFS: ${filename}`);
				return { success: false, error: 'File not found' };
			}
		}
		
		// Try to actually open the file using FS.open to verify access
		try {
			console.log(`Debug: Attempting to open file with FS.open: ${filePath}`);
			const stream = Module.FS.open(filePath, 'r');
			
			// Read a small chunk to verify content
			const buffer = new Uint8Array(100);
			const bytesRead = Module.FS.read(stream, buffer, 0, buffer.length, 0);
			
			// Log the first few bytes as hex for debug purposes
			let hexString = '';
			for (let i = 0; i < Math.min(20, bytesRead); i++) {
				hexString += buffer[i].toString(16).padStart(2, '0') + ' ';
			}
			
			console.log(`Debug: Read ${bytesRead} bytes. First bytes: ${hexString}`);
			
			// Close the file
			Module.FS.close(stream);
			
			return {
				success: true,
				path: filePath,
				bytesRead,
				firstBytes: buffer.slice(0, Math.min(20, bytesRead))
			};
		} catch (error) {
			console.error(`Debug: Error accessing file via WASM FS API: ${filePath}`, error);
			return { success: false, error: error.toString() };
		}
	} catch (error) {
		console.error(`Debug: Error in verifyWasmFileAccess for '${filename}':`, error);
		return { success: false, error: error.toString() };
	}
};

// Utility function to list all files in OPFS for debugging
Module.listOPFSFiles = async () => {
	if (!Module._opfsInitialized) {
		await Module.initOPFS();
	}
	
	try {
		console.log('Debug: Listing all files in OPFS...');
		const root = await navigator.storage.getDirectory();
		
		// This requires a custom function to recursively iterate through entries
		async function collectAllFiles(dirHandle, path = '') {
			const entries = [];
			for await (const [name, handle] of dirHandle.entries()) {
				const nestedPath = path ? `${path}/${name}` : name;
				if (handle.kind === 'directory') {
					const nestedEntries = await collectAllFiles(handle, nestedPath);
					entries.push(...nestedEntries);
				} else {
					const file = await handle.getFile();
					entries.push({
						name: nestedPath,
						size: file.size,
						lastModified: new Date(file.lastModified)
					});
				}
			}
			return entries;
		}
		
		const files = await collectAllFiles(root);
		console.log(`Debug: Found ${files.length} files in OPFS:`, files);
		return files;
	} catch (error) {
		console.error('Debug: Error listing OPFS files:', error);
		return [];
	}
};

// Override create_file_info to add debugging
const originalCreateFileInfo = Module.create_file_info;
if (originalCreateFileInfo) {
    Module.create_file_info = function(hdr, seq) {
        console.log(`Debug: create_file_info called with header=${hdr}, sequence=${seq}`);
        const result = originalCreateFileInfo(hdr, seq);
        console.log(`Debug: create_file_info result: ${result ? 'success' : 'failed'}`);
        return result;
    };
}

// Override create_single_file_info to add debugging
const originalCreateSingleFileInfo = Module.create_single_file_info;
if (originalCreateSingleFileInfo) {
    Module.create_single_file_info = function(file) {
        console.log(`Debug: create_single_file_info called with file=${file}`);
        const result = originalCreateSingleFileInfo(file);
        console.log(`Debug: create_single_file_info result: ${result ? 'success' : 'failed'}`);
        return result;
    };
}

