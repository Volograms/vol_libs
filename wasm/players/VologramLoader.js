import VolWeb from "../vol_web.mjs"; // Assuming MJS version is desired for loader too

const VologramLoader = (glContext) => {
	let _wasm = {};
	let vologram = {};
	const header = {};
	const frame = {};
	vologram.useOPFS = false; // Add useOPFS flag

	const _loadMesh = (frameIdx) => {
		// Ask the vol_geom WASM to read the frame data from the vologram file into `_frame_data`.
		const ret = vologram.read_frame(frameIdx);
		if (!ret) {
			return false;
		}
		frame.isKey = vologram.is_keyframe(frameIdx);

		// Positions - fetch and upload.
		frame.positions = vologram.frame_get_verts();

		if (header.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			frame.normals = vologram.frame_get_norms();
		}

		// Key-Frames also contain texture coordinate and index data.
		if (frame.isKey) {
			vologram.lastKeyframeLoaded = frameIdx;
			// Texture Coordinates - fetch and upload.
			frame.texCoords = vologram.frame_get_uvs();

			// Indices - fetch and upload.
			frame.numIndices = vologram.frame_i_sz() / 2;
			frame.indices = vologram.frame_get_ind();
		}

		if (header.hasTexture && header.hasBasisTexture) {
			vologram.run_basis_transcode(header.basisFormat);
			frame.textureData = vologram.basis_get_data();
		}

		vologram.lastFrameLoaded = frameIdx;
		return frame;
	};

	const _updateMeshFrameAllowingSkip = (desiredFrameIndex) => {
		if (vologram.lastFrameLoaded === desiredFrameIndex) {
			return frame;
		} // Safety catch to avoid reloading the same frame twice.
		if (desiredFrameIndex < 0 || desiredFrameIndex >= header.frameCount) {
			return frame;
		}

		const keyframeRequired = vologram.find_previous_keyframe(desiredFrameIndex);

		// If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
		if (vologram.lastKeyframeLoaded !== keyframeRequired) {
			_loadMesh(keyframeRequired);
		}
		// Load actual current frame.
		return _loadMesh(desiredFrameIndex);
	};

	const _initVologram = () => {
		let ret = false;
		const basePath = vologram.useOPFS ? "/opfs/" : ""; // Determine base path

		if (header.singleFile) {
			ret = vologram.create_single_file_info(basePath + "vologram.vols");
		} else {
			ret = vologram.create_file_info(basePath + "header.vols", basePath + "sequence.vols");
		}

		if (!ret) {
			console.error("failed to load vologram");
			return false;
		}

		_initVologramHeader();

		return true;
	};

	const _initVologramHeader = () => {
		header.textureCompression = vologram.texture_compression();
		header.textureContainerFormat = vologram.texture_container_format();
		header.textureWidth = vologram.texture_width();
		header.textureHeight = vologram.texture_height();
		if (header.textureCompression === 1 || header.textureCompression === 2) {
			header.hasBasisTexture = true;
			if (!glContext) {
				console.error(
					`
					Vologram has basis texture but no rendering context was given. 
					See https://developer.mozilla.org/en-US/docs/Web/API/HTMLCanvasElement/getContext
					for an example of how to obtain the rendering context. 
					`
				);
				header.ready = false;
				return;
			}
			[header.glFormat, header.basisFormat] = vologram.find_basis_fmt(glContext);
		} else {
			header.hasBasisTexture = false;
		}

		header.hasNormals = vologram.has_normals();
		frame.hasNormals = header.hasNormals;
		header.hasTexture = vologram.has_texture();

		header.hasAudio = vologram.has_audio();
		if (header.hasAudio) {
			header.audioUrl = _getAudioData();
		} else {
			header.audioUrl = "";
		}

		header.frameCount = vologram.frame_count();
		header.fps = 30;
		header.durationS = header.frameCount / header.fps; // 5.0;
		header.ready = true;
	};

	const _initWasmSingleFile = async (onProgress) =>
		VolWeb()
			.then((wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				// Conditional fetch based on useOPFS
				if (vologram.useOPFS) {
					console.log("Loader: Using OPFS for single file download...");
					// Loader typically needs the whole file before proceeding, so use fetch_file_opfs
					return _wasm.fetch_file_opfs("vologram.vols", vologram.sequenceUrl, onProgress);
				} else {
					return _wasm.fetch_file("vologram.vols", vologram.sequenceUrl, onProgress);
				}
			})
			.then((response) => {
				return new Promise((resolve, reject) => {
					const initSuccess = _initVologram();
					if (initSuccess) resolve(initSuccess);
					else reject(new Error("_initVologram failed to open vologram"));
				});
			})
			.catch((err) => {
				console.error("Loader: Error during WASM single file initialization:", err);
				return false;
			});

	const _initWasm = (onProgress) =>
		VolWeb()
			.then((wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				// Conditional fetch for header
				if (vologram.useOPFS) {
					console.log("Loader: Using OPFS for header download...");
					return _wasm.fetch_file_opfs("header.vols", vologram.headerUrl, onProgress);
				} else {
					return _wasm.fetch_file("header.vols", vologram.headerUrl, onProgress);
				}
			})
			.then((response) => {
				// Conditional fetch for sequence
				if (vologram.useOPFS) {
					console.log("Loader: Using OPFS for sequence download...");
					return _wasm.fetch_file_opfs("sequence.vols", vologram.sequenceUrl, onProgress);
				} else {
					return _wasm.fetch_file("sequence.vols", vologram.sequenceUrl, onProgress);
				}
			})
			.then((response) => {
				return new Promise((resolve, reject) => {
					const initSuccess = _initVologram();
					if (initSuccess) resolve(initSuccess);
					else reject(new Error("_initVologram failed to open vologram"));
				});
			})
			.catch((err) => {
				console.error("Loader: Error during WASM multi-file initialization:", err);
				return false;
			});

	const _getFrameFromSeconds = (seconds) => {
		return Math.floor(seconds * header.fps) % header.frameCount;
	};

	const _getAudioData = () => {
		const blob = new Blob([vologram.get_audio_data()], { type: "audio/mpeg" });
		const blobUrl = URL.createObjectURL(blob);
		return blobUrl;
	};

	const _open = async ({ headerUrl, sequenceUrl, useOPFS = false }, onProgress) => {
		vologram = {};
		vologram.useOPFS = useOPFS; // Store the OPFS flag
		header.singleFile = !headerUrl;
		vologram.headerUrl = headerUrl;
		vologram.sequenceUrl = sequenceUrl;

		if (header.singleFile) return _initWasmSingleFile(onProgress);
		else return _initWasm(onProgress);
	};

	const _cleanVologramModule = () => {
		_wasm.ccall("basis_free", "boolean");
		vologram.free_file_info();
		const basePath = vologram.useOPFS ? "/opfs/" : "";
		try {
			if (_wasm.FS.analyzePath(basePath + "vologram.vols").exists) {
				_wasm.FS.unlink(basePath + "vologram.vols");
			}
			if (_wasm.FS.analyzePath(basePath + "header.vols").exists) {
				_wasm.FS.unlink(basePath + "header.vols");
			}
			if (_wasm.FS.analyzePath(basePath + "sequence.vols").exists) { // Check sequence.vols too
				_wasm.FS.unlink(basePath + "sequence.vols");
			}
			// Note: sequence_0.vols seems specific to older logic, might not be needed for OPFS/current MEMFS
			if (_wasm.FS.analyzePath("sequence_0.vols").exists) {
				_wasm.FS.unlink("sequence_0.vols");
			}
		} catch (e) {
			console.warn("Loader: Error during FS cleanup (file might not exist):", e);
		}
		_wasm = null;
	};

	const _cleanVologramObject = () => {
		if (header.audioUrl) {
			URL.revokeObjectURL(header.audioUrl);
		}
		header.ready = false;
		vologram.headerUrl = null;
		vologram.sequenceUrl = null;
	};

	const _close = () => {
		_cleanVologramObject();
		_cleanVologramModule();
	};

	return {
		get vologram() {
			return vologram;
		},
		get header() {
			return header;
		},
		get frame() {
			return frame;
		},
		get open() {
			return _open;
		},
		get loadFrame() {
			return _updateMeshFrameAllowingSkip;
		},
		get frameFromSeconds() {
			return _getFrameFromSeconds;
		},
		get close() {
			return _close;
		},
	};
};

// Make available globally or export depending on usage context
// If used via <script>, it might attach to window
// If used as a module, export it
if (typeof exports === 'object' && typeof module === 'object') {
    module.exports = VologramLoader;
} else if (typeof define === 'function' && define.amd) {
    define([], () => VologramLoader);
} else if (typeof window !== 'undefined') {
    window.VologramLoader = VologramLoader;
}
