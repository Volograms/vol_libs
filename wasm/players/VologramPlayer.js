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
		if (vologram.lastFrameLoaded === frameIdx) {
			return false;
		} // Safety catch to avoid reloading the same frame twice.

		// Ask the vol_geom WASM to read the frame data from the vologram file into `_frame_data`.
		const ret = vologram.read_frame(frameIdx);
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
		const keyframeRequired = vologram.find_previous_keyframe(desiredFrameIndex);

		// If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
		if (vologram.lastKeyframeLoaded !== keyframeRequired) {
			_loadMesh(keyframeRequired);
		}
		// Load actual current frame.
		_loadMesh(desiredFrameIndex);
	};

	const _initVologram = () => {
		let ret = false;
		if (vologram.header.singleFile) {
			ret = vologram.create_single_file_info("vologram.vols");
		} else {
			ret = vologram.create_file_info("header.vols", "sequence.vols");
		}

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
		vologram.header.frameCount = vologram.frame_count();
		vologram.header.fps = 30;
		vologram.header.durationS = vologram.header.frameCount / vologram.header.fps; // 5.0;
		vologram.header.ready = true;
	};

	const _initWasmSingleFile = async (onProgress) =>
		VolWeb()
			.then((wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				return _wasm.fetch_file("vologram.vols", vologram.fileUrl, onProgress);
			})
			.then((response) => {
				_initVologram();
				return true;
			});

	const _initWasm = async (onProgress) =>
		VolWeb()
			.then((wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				return _wasm.fetch_file("header.vols", vologram.headerUrl, onProgress);
			})
			.then((response) => _wasm.fetch_file("sequence.vols", vologram.sequenceUrl, onProgress))
			.then((response) => {
				_initVologram();
				return true;
			});

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
		let time = 0;
		if (vologram.attachedAudio) time = vologram.attachedAudio.currentTime;
		else time = now / 1000 - _playbackStartTime;
		if (!_timerPaused && vologram.header.ready && _shouldAdvanceFrame(time)) {
			_updateMeshFrameAllowingSkip(_frameToLoad);
			_events.onframeready.forEach((fn) => fn(vologram));
		}
		if (!_timerPaused) requestAnimationFrame(_frameRequestCallback);
	};

	const play = () => {
		if (vologram.attachedVideo) {
			vologram.attachedVideo.currentTime = 0;
			vologram.attachedVideo.play();
		} else if (vologram.attachedAudio) {
			vologram.attachedAudio.currentTime = 0;
			vologram.attachedAudio.play();
			_timerPaused = false;
			requestAnimationFrame(_frameRequestCallback);
		} else {
			_playbackStartTime = performance.now() / 1000;
			_timerPaused = false;
			requestAnimationFrame(_frameRequestCallback);
		}
	};

	const pause = () => {
		if (vologram.attachedVideo) vologram.attachedVideo.pause();
		else if (vologram.attachedAudio) {
			_timerPaused = true;
			vologram.attachedAudio.pause();
		} else {
			_timerPaused = true;
		}
	};

	const resume = () => {
		if (vologram.attachedVideo) {
			vologram.attachedVideo.play();
		} else if (vologram.attachedAudio) {
			vologram.attachedAudio.play();
			_timerPaused = false;
			requestAnimationFrame(_frameRequestCallback);
		} else {
			_timerPaused = false;
			requestAnimationFrame(_frameRequestCallback);
		}
	};

	const isPlaying = () => {
		if (vologram.attachedVideo) return !vologram.attachedVideo.paused && !vologram.attachedVideo.ended;
		if (vologram.attachedAudio) return !vologram.attachedAudio.paused && !vologram.attachedAudio.ended;
		else return !_timerPaused;
	};

	const mute = (setValue) => {
		if (setValue !== undefined) {
			if (vologram.attachedVideo) vologram.attachedVideo.muted = setValue;
			if (vologram.attachedAudio) vologram.attachedAudio.muted = setValue;
			return setValue;
		} else {
			if (vologram.attachedVideo) return vologram.attachedVideo.muted;
			if (vologram.attachedAudio) return vologram.attachedAudio.muted;
		}
		return false;
	};

	/** @type {(videoElement: HTMLVideoElement) => void} */
	const attachVideo = (videoElement) => {
		vologram.attachedVideo = videoElement;
		videoElement.src = vologram.textureUrl;
		videoElement.requestVideoFrameCallback(_videoFrameCallback);
	};

	const attachAudio = (audioElement) => {
		audioElement.addEventListener("ended", () => {
			_events.onloop.forEach((fn) => fn());
		});
		vologram.attachedAudio = audioElement;
	};

	const createAudio = () => {
		if (!vologram.header.hasAudio) {
			console.warn("Vologram has no audio - cannot create audio element");
			return;
		}
		const audioElmnt = document.createElement("audio");
		document.body.insertAdjacentElement("afterbegin", audioElmnt);
		audioElmnt.hidden = true;
		const blob = new Blob([vologram.get_audio_data()], { type: "audio/mpeg" });
		const blobUrl = URL.createObjectURL(blob);
		window.addEventListener(
			"beforeunload",
			(e) => {
				URL.revokeObjectURL(blobUrl);
			},
			false
		);
		audioElmnt.src = blobUrl;
		attachAudio(audioElmnt);
	};

	const open = async (headerUrl, sequenceUrl, textureUrl, onProgress) => {
		vologram = {};
		vologram.header = {};
		vologram.frame = {};
		vologram.header.singleFile = false;
		vologram.headerUrl = headerUrl;
		vologram.sequenceUrl = sequenceUrl;
		vologram.textureUrl = textureUrl;
		return _initWasm(onProgress);
	};

	const openSingleFile = async (fileUrl, onProgress) => {
		vologram = {};
		vologram.header = {};
		vologram.frame = {};
		vologram.header.singleFile = true;
		vologram.fileUrl = fileUrl;
		return _initWasmSingleFile(onProgress);
	};

	const close = () => {
		_timerPaused = true;
		if (vologram.attachedVideo) {
			vologram.attachedVideo.pause();
			vologram.attachedVideo = null;
		}
		if (vologram.attachedAudio) {
			vologram.attachedAudio.pause();
			vologram.attachedAudio = null;
		}
		_events.onclose.forEach((fn) => fn());
	};

	const addEventListener = (event, callback) => {
		_events[event].push(callback);
	};

	const removeEventListener = (event, callback) => {
		const index = _events[event].indexOf(callback);
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
		attachAudio,
		createAudio,
		open,
		openSingleFile,
		close,
		addEventListener,
		removeEventListener,
	};
};
