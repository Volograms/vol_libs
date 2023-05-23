const VologramPlayer = () => {
	let _wasm = {};
	/** @type {number} */ let _frameToLoad;
	/** @type {boolean} */ let _timerPaused;
	/** @type {number} */ let _playbackStartTime;
	let vologram = {};

	const _events = {
		/** @type {Array<(vologram: any) => void>} */
		onframeready: [],
		/** @type {Array<() => void>} */
		onclose: [],
		/** @type {Array<() => void} */
		onloop: [],
	};

	const _loadMesh = (frameIdx) => {
		if (vologram.lastFrameLoaded == frameIdx) {
			return false;
		} // Safety catch to avoid reloading the same frame twice.

		// Ask the vol_geom WASM to read the frame data from the vologram file into `_frame_data`.
		var ret = vologram.read_frame(frameIdx);
		if (!ret) {
			return false;
		}
		vologram.frame.isKey = vologram.is_keyframe(frameIdx);

		// Positions - fetch and upload.
		vologram.frame.positions = vologram.frame_get_verts();

		if (vologram.header.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			vologram.frame.normals = vologram.frame_get_norms();
		}

		// Key-Frames also contain texture coordinate and index data.
		if (vologram.frame.isKey) {
			vologram.lastKeyframeLoaded = frameIdx;
			// Texture Coordinates - fetch and upload.
			vologram.frame.texCoords = vologram.frame_get_uvs();

			// Indices - fetch and upload.
			vologram.frame.indices = vologram.frame_get_ind();
		}
		vologram.lastFrameLoaded = frameIdx;
		return true;
	};

	// Calls mesh_from_frame() but first loads a keyframe, if required.
	const _updateMeshFrameAllowingSkip = (desiredFrameIndex) => {
		var keyframeRequired = vologram.find_previous_keyframe(desiredFrameIndex);

		// If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
		if (vologram.lastKeyframeLoaded != keyframeRequired) {
			_loadMesh(keyframeRequired);
		}
		// Load actual current frame.
		_loadMesh(desiredFrameIndex);
	};

	const _initVologram = () => {
		var ret = false;
		if (vologram.header.singleFile) {
			ret = vologram.create_single_file_info("vologram.vols");
		} else {
			ret = vologram.create_file_info("header.vols", "sequence.vols");
		}

		console.log("create_file_info=" + ret);
		if (!ret) {
			console.error("failed to load vologram");
			return;
		}

		vologram.header.hasNormals = vologram.has_normals();
		vologram.header.hasTexture = vologram.has_texture();
		vologram.header.hasAudio = vologram.has_audio();
		vologram.header.textureCompression = vologram.texture_compression();
		vologram.header.textureContainerFormat = vologram.texture_container_format();
		vologram.header.textureWidth = vologram.texture_width();
		vologram.header.textureHeight = vologram.texture_height();
		if (vologram.header.textureWidth === 0) vologram.header.textureWidth = 2048;
		if (vologram.header.textureHeight === 0) vologram.header.textureHeight = 2048;
		console.log(vologram.header.textureWidth, vologram.header.textureHeight);
		vologram.header.frameCount = vologram.frame_count();
		vologram.header.fps = 30;
		vologram.header.durationS = vologram.header.frameCount / vologram.header.fps; // 5.0;
		vologram.header.ready = true;
		console.log(vologram);
	};

	const _initWasmSingleFile = async (onProgress) => {
		return VolWeb()
			.then((wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				return _wasm.fetch_file("vologram.vols", vologram.fileUrl, onProgress);
			})
			.then((response) => {
				console.log(response);
				_initVologram();
				return true;
			});
	};

	const _initWasm = async (onProgress) => {
		return VolWeb()
			.then((wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				return _wasm.fetch_file("header.vols", vologram.headerUrl, onProgress);
			})
			.then((response) => {
				console.log(response);
				return _wasm.fetch_file("sequence.vols", vologram.sequenceUrl, onProgress);
			})
			.then((response) => {
				console.log(response);
				_initVologram();
				return true;
			});
	};

	const _shouldAdvanceFrame = (time) => {
		_frameToLoad = Math.floor(vologram.header.fps * time);
		if (_frameToLoad === vologram.lastFrameLoaded) {
			return false;
		}
		if (_frameToLoad >= vologram.header.frameCount) {
			_frameToLoad = 0;
			_playbackStartTime = performance.now() / 1000;
			_events.onloop.forEach((fn) => fn());
		}
		vologram.lastUpdateTime = time;
		return true;
	};

	/** @type {VideoFrameRequestCallback} */
	const _videoFrameCallback = (now, metadata) => {
		if (vologram.header.ready && _shouldAdvanceFrame(metadata.mediaTime)) {
			_updateMeshFrameAllowingSkip(_frameToLoad);
			_events.onframeready.forEach((fn) => fn(vologram));
		}
		if (vologram.attachedVideo) {
			vologram.attachedVideo.requestVideoFrameCallback(_videoFrameCallback);
		}
	};

	/** @type {FrameRequestCallback} */
	const _frameRequestCallback = (now) => {
		if (!_timerPaused && vologram.header.ready && _shouldAdvanceFrame(now / 1000 - _playbackStartTime)) {
			_updateMeshFrameAllowingSkip(_frameToLoad);
			_events.onframeready.forEach((fn) => fn(vologram));
		}
		if (!_timerPaused) requestAnimationFrame(_frameRequestCallback);
	};

	const play = () => {
		console.log("play");
		if (vologram.attachedVideo) {
			vologram.attachedVideo.currentTime = 0;
			vologram.attachedVideo.play();
		} else {
			_playbackStartTime = performance.now() / 1000;
			_timerPaused = false;
			requestAnimationFrame(_frameRequestCallback);
		}
	};

	const pause = () => {
		if (vologram.attachedVideo) vologram.attachedVideo.pause();
		else {
			_timerPaused = true;
		}
	};

	const resume = () => {
		if (vologram.attachedVideo) {
			vologram.attachedVideo.play();
		} else {
			_timerPaused = false;
			requestAnimationFrame(_frameRequestCallback);
		}
	};

	const isPlaying = () => {
		if (vologram.attachedVideo) return !vologram.attachedVideo.paused && !vologram.attachedVideo.ended;
		else return !_timerPaused;
	};

	const mute = (setValue) => {
		if (setValue) {
			if (vologram.attachedVideo) vologram.attachedVideo.muted = setValue;
			// TODO: Audio Element
			return setValue;
		} else {
			if (vologram.attachedVideo) return vologram.attachedVideo.muted;
			// TODO: Audio Element
		}
		return false;
	};

	/** @type {(videoElement: HTMLVideoElement) => void} */
	const attachVideo = (videoElement) => {
		vologram.attachedVideo = videoElement;
		videoElement.src = vologram.textureUrl;
		videoElement.requestVideoFrameCallback(_videoFrameCallback);
	};

	const open = async (headerUrl, sequenceUrl, textureUrl, onProgress) => {
		vologram = {};
		vologram.header = {};
		vologram.frame = {};
		vologram.header.singleFile = false;
		vologram.headerUrl = headerUrl;
		vologram.sequenceUrl = sequenceUrl;
		vologram.textureUrl = textureUrl;
		return _initWasm(onProgress).then((w) => {
			console.log(w);
		});
	};

	const openSingleFile = async (fileUrl, onProgress) => {
		vologram = {};
		vologram.header = {};
		vologram.frame = {};
		vologram.header.singleFile = true;
		vologram.fileUrl = fileUrl;
		return _initWasmSingleFile(onProgress).then((w) => {
			console.log(w);
		});
	};

	const close = () => {
		_timerPaused = true;
		vologram.attachedVideo?.pause();
		vologram.attachedVideo = null;
		_events.onclose.forEach((fn) => fn());
	};

	const addEventListener = (event, callback) => {
		_events[event].push(callback);
	};

	const removeEventListener = (event, callback) => {
		let index = _events[event].indexOf(callback);
		if (index < 0) return;
		_events[event].splice(index, 1);
	};

	const getVologram = () => vologram;

	return {
		getVologram,
		play,
		pause,
		resume,
		isPlaying,
		mute,
		attachVideo,
		open,
		openSingleFile,
		close,
		addEventListener,
		removeEventListener,
	};
};
