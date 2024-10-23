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

Module.fetch_stream_file = async (dest, fileUrl, onProgress) => {

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
			fetchStarted = true;

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
				// Read some more, and call this function again
				if (seekLocation > 60) {
					Module.headerFetched = true;
					// return  reader.read().then(pump);
				}
				return reader.read().then(pump);
			})

			// return new ReadableStream({
			// 	start(controller) {
			// 		return pump(0);
			// 		function pump(start) {
			// 			return reader.read().then(({ done, value }) => {
			// 				// When no more data needs to be consumed, close the stream
			// 				if (done) {
			// 					Module.FS.close(fileStream);
			// 					controller.close();
			// 					console.log(('Fetching stream finished.'));
			// 					return;
			// 				}
			// 				// Enqueue the next data chunk into our target stream
			// 				Module.FS.write(fileStream, value, start, value.length, 0);
			// 				controller.enqueue(value);
			// 				return pump(start + value.length);
			// 			});
			// 		}
			// 	},
			// });
		})
		// .then((stream) => new Response(stream))
		.then((url) => console.log(('Stream ready.')))
		.catch((err) => console.error(err));

	// await fetch(fileUrl)
	// 	// Retrieve its body as ReadableStream
	// 	.then((response) => {
	// 		if (!response.ok) {
	// 			throw new Error(`HTTP error! Status: ${response.status}`);
	// 		}
	// 		return response.arrayBuffer();
	// 	})
	// 	.then((arrayBuffer) => {
	// 		const byteArray = new Uint8Array(arrayBuffer);
	// 		var stream = Module.FS.open(dest, "w");
	// 		Module.FS.write(stream, byteArray, 0, byteArray.length, 0);
	// 		Module.FS.close(stream);

	// 	})
	// 	.then((url) => console.log(('Stream ready.')))
	// 	.catch((err) => console.error(err));
};

Module.fetch_file = async (dest, fileUrl, onProgress) => {

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
		// .then((stream) => new Response(stream))
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
