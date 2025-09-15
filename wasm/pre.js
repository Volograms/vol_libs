// Module.fetch_file = (dest, fileUrl, onProgress) => {
// 	return new Promise((resolve, reject) => {
// 		const xhr = new XMLHttpRequest();
// 		xhr.open("GET", fileUrl, true);
// 		xhr.responseType = "arraybuffer";
// 		xhr.onprogress = (e) => {
// 			if (onProgress) {
// 				onProgress(e.loaded / e.total);
// 			}
// 		};
// 		xhr.onload = () => {
// 			if (!xhr.response) {
// 				reject(new Error("No response received"));
// 				return;
// 			}
// 			const byteArray = new Uint8Array(xhr.response);
// 			var stream = Module.FS.open(dest, "w");
// 			Module.FS.write(stream, byteArray, 0, byteArray.length, 0);
// 			Module.FS.close(stream);
// 			resolve({ status: xhr.status, responseUrl: xhr.responseURL });
// 		};
// 		xhr.onerror = () => reject(new Error("Download failed"));
// 		xhr.onabort = () => reject(new Error("Download aborted"));
// 		xhr.send(null);
// 	});
// };

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

Module.fetch_stream_file = (dest, fileUrl, onProgress, abortSignal = null) => {

	// Create fetch options with abort signal if provided
	const fetchOptions = {};
	if (abortSignal) {
		fetchOptions.signal = abortSignal;
	}

	let resolveHeaderLoaded;
	const headerLoadedPromise = new Promise((resolve) => {
		resolveHeaderLoaded = resolve;
	});

	const bufferSize = 5*1024*1024; 
	var fileStream = null;

	const downloadFinishedPromise = fetch(fileUrl, fetchOptions)
		// Retrieve its body as ReadableStream
		.then(async (response) => {
			if (!response.ok) {
				throw new Error(`HTTP error! Status: ${response.status}`);
			}
			const reader = response.body.getReader();
			fileStream = Module.FS.open(dest, "w");
			var seekLocation = 0;
			var fileSize = response.headers.get("content-length");
			let headerResolved = false;

			await reader.read().then(function pump({ done, value }) {
				if (onProgress) {
					onProgress(seekLocation/fileSize);
				}

				if (done) {
					// Do something with last chunk of data then exit reader
					Module.fileFetched = true;
					//Module.FS.close(fileStream); // This will be handled in 'finally'
					console.log(('Fetching stream finished.'));
					if (!headerResolved) {
						resolveHeaderLoaded();
					}
					return;
				}
				// Otherwise do something here to process current chunk
				Module.FS.write(fileStream, value, 0, value.length, seekLocation);

				seekLocation += value.length
				// Read header and a few frames before initializing vologram
				if (!headerResolved && seekLocation > bufferSize) {
					Module.headerFetched = true;
					headerResolved = true;
					resolveHeaderLoaded();
				}
				return reader.read().then(pump);
			})
		})
		.finally(() => {
			if (fileStream) {
				Module.FS.close(fileStream);
				fileStream = null;
			}
		})
		.catch((err) => {
			if (err.name === "AbortError") {
				console.log("Download aborted.");
			} else {
				console.error("Download error:", err);
			}
			throw err; // Re-throw to allow promise chain to catch it
		});

	console.log("Stream manager created, download starting.");
	return {
		headerLoaded: headerLoadedPromise,
		downloadFinished: downloadFinishedPromise,
	};
};

//
// ===== ENHANCED STREAMING WITH CIRCULAR BUFFER =====
// New streaming function that uses the C++ circular buffer for large files
//

Module.fetch_stream_buffer = (dest, fileUrl, config, onProgress, abortSignal = null) => {
	// Initialize streaming configuration with defaults if not provided
	if (!config) {
		console.log('Initializing streaming config');
		Module.init_streaming_config();
		config = {
			maxBufferSize: Module.get_max_buffer_size(),
			lookaheadSeconds: Module.get_lookahead_seconds(),
			autoSelectMode: Module.get_auto_select_mode()
		};
	}

	// Create fetch options with abort signal if provided
	const fetchOptions = {};
	if (abortSignal) {
		fetchOptions.signal = abortSignal;
	}

	let resolveHeaderLoaded;
	const headerLoadedPromise = new Promise((resolve) => {
		resolveHeaderLoaded = resolve;
	});

	let streamingMode = false;
	let fileSize = 0;
	let downloadPaused = false;
	let currentFrame = 0;

	// Backpressure gate: when paused, do not call reader.read(); wait here instead
	let _resumeResolver = null;
	const _waitForResume = () => {
		if (!downloadPaused) return Promise.resolve();
		return new Promise((resolve) => { _resumeResolver = resolve; });
	};
	const _resumeNow = () => {
		if (!downloadPaused) return;
		downloadPaused = false;
		if (_resumeResolver) { const r = _resumeResolver; _resumeResolver = null; r(); }
	};

	// Optional Range mode (8MB default)
	const useRangeRequests = !!(config && (config.useRangeRequests || config.rangeChunkBytes));
	const rangeChunkBytes = (config && config.rangeChunkBytes) ? config.rangeChunkBytes : (8 * 1024 * 1024);

	const downloadFinishedPromise = fetch(fileUrl, fetchOptions)
		.then(async (response) => {
			if (!response.ok) {
				throw new Error(`HTTP error! Status: ${response.status}`);
			}

			// Get file size and apply streaming config FIRST
			const contentLength = response.headers.get("content-length");
			fileSize = contentLength ? parseInt(contentLength) : 0;
			
			// Apply streaming configuration BEFORE making the mode decision
			if (config.maxBufferSize) Module.set_max_buffer_size(config.maxBufferSize);
			if (config.lookaheadSeconds) Module.set_lookahead_seconds(config.lookaheadSeconds);
			if (config.autoSelectMode !== undefined) Module.set_auto_select_mode(config.autoSelectMode);
			if (config.forceStreamingMode !== undefined) Module.set_force_streaming_mode(config.forceStreamingMode);
			
			// Now make the streaming mode decision with the correct config applied
			streamingMode = Module.should_use_streaming_mode(fileSize);
			const a = Module.should_use_streaming_mode(fileSize);
			console.log('Streaming mode:', streamingMode);
			console.log('Streaming mode:', a);
			
			console.log(`File size: ${fileSize ? (fileSize/1024/1024).toFixed(1) + 'MB' : 'unknown'}, using ${streamingMode ? 'streaming' : 'full download'} mode`);

			let seekLocation = 0;
			let headerResolved = false;

			// For streaming mode, create the circular buffer
			if (streamingMode) {

				if (!Module.create_streaming_buffer()) {
					throw new Error("Failed to create streaming buffer");
				}
				console.log(`Created circular buffer: ${(Module.get_max_buffer_size()/1024/1024).toFixed(1)}MB capacity`);

				// If using Range requests, run segmented download loop and return
				if (useRangeRequests) {
					const runRangeLoop = async () => {
						while (true) {
							if (onProgress && fileSize > 0) {
								onProgress(seekLocation / fileSize);
							}

							// Ensure space for next chunk; attempt compaction if needed
							const maxSize = Module.get_max_buffer_size ? Module.get_max_buffer_size() : 0;
							const usedSize = Module.get_playback_buffer_size ? Module.get_playback_buffer_size() : 0;
							const freeSpace = maxSize > 0 ? (maxSize - usedSize) : Number.MAX_SAFE_INTEGER;
							const SAFETY_HEADROOM = 64 * 1024;
							if (Module.fileFetched) break; // stop requesting if finished
							if (downloadPaused || freeSpace < (rangeChunkBytes + SAFETY_HEADROOM)) {
								downloadPaused = true;
								if (Module.should_resume_download) { Module.should_resume_download(currentFrame, 30.0); }
								if (Module.swap_buffers && Module.swap_buffers()) {
									const used2 = Module.get_playback_buffer_size ? Module.get_playback_buffer_size() : 0;
									const free2 = maxSize > 0 ? (maxSize - used2) : Number.MAX_SAFE_INTEGER;
									if (free2 >= (rangeChunkBytes + SAFETY_HEADROOM)) {
										downloadPaused = false;
									}
								}
								if (downloadPaused) { await _waitForResume(); continue; }
							}

							if (fileSize > 0 && seekLocation >= fileSize) break; // EOF

							const end = fileSize > 0 ? Math.min(seekLocation + rangeChunkBytes, fileSize) - 1 : (seekLocation + rangeChunkBytes - 1);
							const headers = { Range: `bytes=${seekLocation}-${end}` };
							const res = await fetch(fileUrl, { headers, signal: abortSignal });
							if (!(res.status === 206 || res.status === 200)) {
								throw new Error(`Unexpected status for range request: ${res.status}`);
							}
							const buf = await res.arrayBuffer();
							const len = buf.byteLength;
							if (len === 0) break;

							const dataPtr = Module._malloc(len);
							Module.HEAP8.set(new Uint8Array(buf), dataPtr);
							const ok = Module.add_data_to_buffer(dataPtr, len);
							Module._free(dataPtr);
							if (!ok) {
								downloadPaused = true;
								await _waitForResume();
								continue;
							}

							if (Module.update_buffer_frame_directory) Module.update_buffer_frame_directory();

							seekLocation += len;

							if (!headerResolved && seekLocation > (config.headerThreshold || 5*1024*1024)) {
								Module.headerFetched = true;
								headerResolved = true;
								resolveHeaderLoaded();
							}

							// Fullness handled pre-request; no-op here
						}
						Module.fileFetched = true;
					};

					return runRangeLoop();
				}
			} else {
				// Use traditional file-based approach for small files
				var fileStream = Module.FS.open(dest, "w");
			}

			// For the non-Range path, use the reader API
			const reader = response.body.getReader();

			// Main download loop
			await reader.read().then(function pump({ done, value }) {
				if (onProgress && fileSize > 0) {
					onProgress(seekLocation / fileSize);
				}

				if (done) {
					Module.fileFetched = true;
					console.log('Download finished.');
					if (!headerResolved) {
						resolveHeaderLoaded();
					}
					return;
				}

				// Add data to appropriate destination
				if (streamingMode) {
					// Ensure there is space for this chunk; attempt compaction if needed
					const maxSize = Module.get_max_buffer_size ? Module.get_max_buffer_size() : 0;
					const usedSize = Module.get_playback_buffer_size ? Module.get_playback_buffer_size() : 0;
					const freeSpace = maxSize > 0 ? (maxSize - usedSize) : Number.MAX_SAFE_INTEGER;
					const SAFETY_HEADROOM = 64 * 1024;
					if (Module.fileFetched) { return reader.read().then(pump); }
					if (downloadPaused || freeSpace < (value.length + SAFETY_HEADROOM)) {
						console.log('Download paused due to buffer full.');
						console.log('Free space:', freeSpace);
						console.log('Value length:', value.length);
						console.log('Safety headroom:', SAFETY_HEADROOM);
						downloadPaused = true;
						if (Module.should_resume_download) { Module.should_resume_download(currentFrame, 30.0); }
						if (Module.swap_buffers && Module.swap_buffers()) {
							const used2 = Module.get_playback_buffer_size ? Module.get_playback_buffer_size() : 0;
							const free2 = maxSize > 0 ? (maxSize - used2) : Number.MAX_SAFE_INTEGER;
							if (free2 >= (value.length + SAFETY_HEADROOM)) {
								downloadPaused = false;
								console.log('Download resumed due to buffer space.');
								console.log('Used space:', used2);
								console.log('Free space:', free2);
							}
						}
						if (downloadPaused) {
							console.log('Pausing download, will re-process chunk after space is freed.');
							return _waitForResume().then(() => pump({ done: false, value: value }));
						}
					}
					
					// Add chunk to buffer
					const dataPtr = Module._malloc(value.length);
					Module.HEAP8.set(value, dataPtr);
					if (!Module.add_data_to_buffer(dataPtr, value.length)) {
						Module._free(dataPtr);
						downloadPaused = true;
						return _waitForResume().then(() => reader.read().then(pump));
					}
					Module._free(dataPtr);

					// Update frame directory
					Module.update_buffer_frame_directory();

					// Fullness handled before reads

				} else {
					// Traditional file write for small files
					Module.FS.write(fileStream, value, 0, value.length, seekLocation);
				}

				seekLocation += value.length;

				// Resolve header promise once we have enough data
				if (!headerResolved && seekLocation > (config.headerThreshold || 5*1024*1024)) {
					Module.headerFetched = true;
					headerResolved = true;
					resolveHeaderLoaded();
				}

				return reader.read().then(pump);
			});
		})
		.finally(() => {
			// Clean up file stream for non-streaming mode
			if (!streamingMode && typeof fileStream !== 'undefined' && fileStream) {
				Module.FS.close(fileStream);
			}
			console.log('Download finished.');
		})
		.catch((err) => {
			if (err.name === "AbortError") {
				console.log("Download aborted.");
			} else {
				console.error("Download error:", err);
			}
			throw err;
		});

	console.log(`Enhanced streaming manager created (${streamingMode ? 'buffer' : 'file'} mode)`);
	
	return {
		headerLoaded: headerLoadedPromise,
		downloadFinished: downloadFinishedPromise,
		isStreamingMode: () => streamingMode,
		pauseDownload: () => { downloadPaused = true; },
		resumeDownload: () => { _resumeNow(); },
		isPaused: () => downloadPaused,
		setCurrentFrame: (frame) => {
			currentFrame = frame;
			// Debug: trace playback advancement
			try { if (console && console.debug) console.debug(`[stream] setCurrentFrame=${currentFrame}`); } catch (e) {}
			// If download is paused due to capacity, try to compact and resume when playback advances
			if (downloadPaused && streamingMode) {
				try {
					if (Module.should_resume_download) { Module.should_resume_download(currentFrame, 30.0); }
					const maxSize = Module.get_max_buffer_size ? Module.get_max_buffer_size() : 0;
					const SAFETY_HEADROOM = 64 * 1024;
					let usedBefore = Module.get_playback_buffer_size ? Module.get_playback_buffer_size() : 0;
					// Attempt compaction multiple times until space is available or no progress
					for (let i = 0; i < 3 && downloadPaused; i++) {
						if (!(Module.swap_buffers && Module.swap_buffers())) break;
						const usedAfter = Module.get_playback_buffer_size ? Module.get_playback_buffer_size() : usedBefore;
						if (usedAfter >= usedBefore) break; // no progress
						usedBefore = usedAfter;
						const freeAfter = maxSize > 0 ? (maxSize - usedAfter) : 0;
						if (freeAfter > SAFETY_HEADROOM) {
							try { if (console && console.debug) console.debug(`[stream] resume after compaction, free=${(freeAfter/1024/1024).toFixed(2)}MB`); } catch (e) {}
							_resumeNow();
							break;
						}
					}
				} catch (e) {
					// noop
				}
			}
		}
	};
};

Module.fetch_file = async (dest, fileUrl, onProgress, abortSignal = null) => {
	// Create fetch options with abort signal if provided
	const fetchOptions = {};
	if (abortSignal) {
		fetchOptions.signal = abortSignal;
	}

	return await fetch(fileUrl, fetchOptions)
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
			.catch((err) => {
				if (err.name === 'AbortError') {
					console.log('Download aborted.');
				} else {
					console.error('Download error:', err);
				}
				// Module.FS.close(fileStream);
				return;
			});
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
	insertObject["basis_get_transcoded_sz_v2"] = Module.cwrap("basis_get_transcoded_sz_v2", "number");
	insertObject["basis_get_transcoded_width"] = Module.cwrap("basis_get_transcoded_width", "number");
	insertObject["basis_get_transcoded_height"] = Module.cwrap("basis_get_transcoded_height", "number");

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

	//
	// ===== STREAMING BUFFER FUNCTIONS =====
	// Enhanced streaming capabilities with circular buffer support
	//

	// Configuration functions
	insertObject["init_streaming_config"] = Module.cwrap("init_streaming_config", "boolean");
	insertObject["should_use_streaming_mode"] = function(fileSize) {
		return !!Module.ccall("should_use_streaming_mode", "number", ["number"], [fileSize]);
	};
	insertObject["get_max_buffer_size"] = Module.cwrap("get_max_buffer_size", "number");
	insertObject["set_max_buffer_size"] = Module.cwrap("set_max_buffer_size", null, ["number"]);
	insertObject["get_min_buffer_size"] = Module.cwrap("get_min_buffer_size", "number");
	insertObject["set_min_buffer_size"] = Module.cwrap("set_min_buffer_size", null, ["number"]);
	insertObject["get_lookahead_seconds"] = Module.cwrap("get_lookahead_seconds", "number");
	insertObject["set_lookahead_seconds"] = Module.cwrap("set_lookahead_seconds", null, ["number"]);
	insertObject["get_auto_select_mode"] = function() {
		return !!Module.ccall("get_auto_select_mode", "number");
	};
	insertObject["set_auto_select_mode"] = Module.cwrap("set_auto_select_mode", null, ["number"]);
	insertObject["get_force_streaming_mode"] = function() {
		return !!Module.ccall("get_force_streaming_mode", "number");
	};
	insertObject["set_force_streaming_mode"] = Module.cwrap("set_force_streaming_mode", null, ["number"]);

	// Buffer management functions  
	insertObject["create_streaming_buffer"] = function() {
		return !!Module.ccall("create_streaming_buffer", "number");
	};
	insertObject["add_data_to_buffer"] = function(dataPtr, dataSize) {
		return !!Module.ccall("add_data_to_buffer", "number", ["number", "number"], [dataPtr, dataSize]);
	};
	insertObject["update_buffer_frame_directory"] = function() {
		return !!Module.ccall("update_buffer_frame_directory", "number");
	};

	// Frame reading functions
	insertObject["read_frame_streaming"] = function(frameIdx) {
		return !!Module.ccall("read_frame_streaming", "number", ["number"], [frameIdx]);
	};
	insertObject["is_frame_available_in_buffer"] = function(frameIdx) {
		return !!Module.ccall("is_frame_available_in_buffer", "number", ["number"], [frameIdx]);
	};

	// Buffer health monitoring
	insertObject["get_buffer_health_bytes"] = Module.cwrap("get_buffer_health_bytes", "number");
	insertObject["get_buffer_health_seconds"] = Module.cwrap("get_buffer_health_seconds", "number", ["number"]);
	insertObject["should_resume_download"] = function(currentFrame, fps) {
		// If full file already fetched, never request resume
		if (Module.fileFetched) return false;
		return !!Module.ccall("should_resume_download", "number", ["number", "number"], [currentFrame, fps]);
	};

	if (usingExternalObject) {
		insertObject.HEAP8 = Module.HEAP8;
		insertObject._malloc = Module._malloc;
		insertObject._free = Module._free;
	}
	
	// Also expose streaming functions directly on Module for fetch_stream_buffer compatibility
	Module.init_streaming_config = insertObject.init_streaming_config;
	Module.get_max_buffer_size = insertObject.get_max_buffer_size;
	Module.set_max_buffer_size = insertObject.set_max_buffer_size;
	Module.get_min_buffer_size = insertObject.get_min_buffer_size;
	Module.set_min_buffer_size = insertObject.set_min_buffer_size;
	Module.get_lookahead_seconds = insertObject.get_lookahead_seconds;
	Module.set_lookahead_seconds = insertObject.set_lookahead_seconds;
	Module.get_auto_select_mode = insertObject.get_auto_select_mode;
	Module.set_auto_select_mode = insertObject.set_auto_select_mode;
	Module.get_force_streaming_mode = insertObject.get_force_streaming_mode;
	Module.set_force_streaming_mode = insertObject.set_force_streaming_mode;
	Module.should_use_streaming_mode = insertObject.should_use_streaming_mode;
	Module.create_streaming_buffer = insertObject.create_streaming_buffer;
	Module.add_data_to_buffer = insertObject.add_data_to_buffer;
	Module.update_buffer_frame_directory = insertObject.update_buffer_frame_directory;
	
	// Dual buffer API functions
	insertObject["is_download_buffer_full"] = function() {
		return !!Module.ccall("is_download_buffer_full", "number");
	};
	insertObject["swap_buffers"] = function() {
		return !!Module.ccall("swap_buffers", "number");
	};
	insertObject["get_playback_buffer_size"] = Module.cwrap("get_playback_buffer_size", "number");
	
	// Also expose dual buffer functions directly on Module
	Module.is_download_buffer_full = insertObject.is_download_buffer_full;
	Module.swap_buffers = insertObject.swap_buffers;
	Module.get_playback_buffer_size = insertObject.get_playback_buffer_size;
	
	// Streaming file info creation
	insertObject["create_streaming_file_info"] = Module.cwrap("create_streaming_file_info", "number");
	Module.create_streaming_file_info = insertObject.create_streaming_file_info;
};
