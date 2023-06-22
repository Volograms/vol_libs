/** @typedef {import('../types/VologramPlayer').VologramPlayerConstructor} VologramPlayerConstructor */
/** @typedef {import('../types/Vologram').Vologram} Vologram */

/** @type {VologramPlayerConstructor} */
const VologramPlayer = (extensions) => {
	extensions = extensions || [];
	const _extensionExports = {};
	let _wasm = {};
	/** @type {number} */ let _frameRequestId;
	/** @type {number} */ let _frameFromTime;
	/** @type {boolean} */ let _timerPaused;
	/** @type {number} */ let _previousTime;
	/** @type {number} */ let _timer;
	/** @type {Vologram} */ let vologram = {};

	const PB_TIMER = 0;
	const PB_VIDEO = 1;
	const PB_AUDIO = 2;

	let _playbackMode = PB_TIMER;

	const _events = {
		/** @type {Array<(vologram: any) => void>} */
		onframeready: [],
		/** @type {Array<() => void>} */
		onclose: [],
		/** @type {Array<() => void} */
		onloop: [],
	};

	/** @type {(frameIdx: number) => boolean} _loadMesh */
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
			vologram.frame.numIndices = vologram.frame_i_sz / 2;
			vologram.frame.indices = vologram.frame_get_ind();
		}
		vologram.lastFrameLoaded = frameIdx;
		return true;
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
		if (!vologram.header.hasTexture && vologram.textureUrl && !vologram.attachedVideo) {
			createVideo();
		}
		vologram.header.hasAudio = vologram.has_audio();
		if (vologram.header.hasAudio && !vologram.attachedAudio) {
			createAudio();
		}
		vologram.header.textureCompression = vologram.texture_compression();
		vologram.header.textureContainerFormat = vologram.texture_container_format();
		vologram.header.textureWidth = vologram.texture_width();
		vologram.header.textureHeight = vologram.texture_height();
		if (vologram.header.textureCompression === 1 || vologram.header.textureCompression === 2) {
			vologram.header.hasBasisTexture = true;
		}
		vologram.header.frameCount = vologram.frame_count();
		vologram.header.fps = 30;
		vologram.header.durationS = vologram.header.frameCount / vologram.header.fps; // 5.0;
		vologram.header.ready = true;

		if (vologram.attachedVideo) {
			_playbackMode = PB_VIDEO;
		} else if (vologram.attachedAudio) {
			_playbackMode = PB_AUDIO;
		} else {
			_playbackMode = PB_TIMER;
		}

		// Initialise Extensions e.g. ThreeJsPlayer or WebGlPlayer
		extensions.forEach((ext) => {
			ext.init(vologram);
			_extensionExports[ext.name] = ext.exports;
		});
	};

	const _initWasmSingleFile = async (onProgress) =>
		VolWeb()
			.then((wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				return _wasm.fetch_file("vologram.vols", vologram.sequenceUrl, onProgress);
			})
			.then((response) => {
				_initVologram();
				return true;
			});

	const _initWasm = (onProgress) =>
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
			})
			.catch((err) => {
				console.error(err);
				return false;
			});

	const _getFrameFromSeconds = (seconds) => {
		_frameFromTime = Math.floor(seconds * vologram.header.fps);
	};

	const _timeTick = (nowTimestamp) => {
		if (_timerPaused) return;
		const delta = Math.max(0, nowTimestamp - _previousTime);
		_timer += delta;
		_previousTime = nowTimestamp;

		_getFrameFromSeconds(_timer / 1000);
		if (_settingFrom == "" && _frameFromTime >= vologram.header.frameCount) {
			_events.onloop.forEach((fn) => fn());
			_frameFromTime = 0;
			_timer = 0;
		}
	};

	const _startTimer = () => {
		_previousTime = performance.now();
		_timerPaused = false;
		_frameFromTime = 0;
		_timer = 0;
	};

	const _updateFrameFromVideo = (now, metadata) => {
		if (vologram.header && vologram.header.ready) {
			_getFrameFromSeconds(metadata.mediaTime);
			_loadMesh(_frameFromTime);
		}
		_frameRequestId = vologram.attachedVideo.requestVideoFrameCallback(_updateFrameFromVideo); // Re-register the callback to be notified about the next frame.
	};

	const _updateFrameFromTimer = (now) => {
		if (vologram.header && vologram.header.ready) {
			_timeTick(now);
			_loadMesh(_frameFromTime);
		}
		_frameRequestId = requestAnimationFrame(_updateFrameFromTimer);
	};

	const _updateFrameFromAudio = () => {
		if (vologram.header && vologram.header.ready) {
			_getFrameFromSeconds(vologram.attachedAudio.currentTime);
			_loadMesh(_frameFromTime);
		}
		_frameRequestId = requestAnimationFrame(_updateFrameFromAudio);
	};

	const attachVideo = (videoElement) => {
		vologram.attachedVideo = videoElement;
		videoElement.src = vologram.textureUrl;
		_frameRequestId = videoElement.requestVideoFrameCallback(_updateFrameFromVideo);
	};

	const createVideo = () => {
		const videoElmnt = document.createElement("video");
		document.body.insertAdjacentElement("afterbegin", videoElmnt);
		videoElmnt.hidden = true;
		attachVideo(videoElmnt);
	};

	const attachAudio = (audioElement) => {
		audioElement.addEventListener("ended", () => {
			_events.onloop.forEach((fn) => fn());
			audioElement.play();
		});
		vologram.attachedAudio = audioElement;
		_frameRequestId = requestAnimationFrame(_updateFrameFromAudio);
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

	const open = async ({ headerUrl, sequenceUrl, textureUrl, videoElement, audioElement }, onProgress) => {
		vologram = {};
		vologram.header = {};
		vologram.frame = {};
		vologram.header.singleFile = !headerUrl;
		vologram.headerUrl = headerUrl;
		vologram.sequenceUrl = sequenceUrl;
		vologram.textureUrl = textureUrl;
		if (videoElement && audioElement) {
			console.warn("Using both video and audio elements is not supported, audio element will be ignored");
		}
		if (videoElement) attachVideo(videoElement);
		else if (audioElement) attachAudio(audioElement);

		if (vologram.header.singleFile) return _initWasmSingleFile(onProgress);
		else return _initWasm(onProgress);
	};

	const _cleanVologramModule = () => {
		_wasm.ccall("basis_free", "boolean");
		vologram.free_file_info();
		if (_wasm.FS.analyzePath("vologram.vols").exists) {
			_wasm.FS.unlink("vologram.vols");
		}
		if (_wasm.FS.analyzePath("header.vols").exists) {
			_wasm.FS.unlink("header.vols");
		}
		if (_wasm.FS.analyzePath("sequence_0.vols").exists) {
			_wasm.FS.unlink("sequence_0.vols");
		}
		_wasm = null;
	};

	const _cleanVologramObject = () => {
		vologram.header.singleFile = false;
		vologram.header.ready = false;
		vologram.headerUrl = null;
		vologram.sequenceUrl = null;
		vologram.textureUrl = null;
	};

	const close = () => {
		if (_frameRequestId && !vologram.attachedVideo) cancelAnimationFrame(_frameRequestId);

		_timerPaused = true;
		if (vologram.attachedVideo) {
			vologram.attachedVideo.cancelVideoFrameCallback(_frameRequestId);
			vologram.attachedVideo.pause();
			vologram.attachedVideo = null;
		}
		if (vologram.attachedAudio) {
			vologram.attachedAudio.pause();
			vologram.attachedAudio = null;
		}
		extensions.forEach((ext) => {
			if (ext.close) ext.close();
		});
		_events.onclose.forEach((fn) => fn());
		_cleanVologramObject();
		_cleanVologramModule();
	};

	const registerCallback = (event, callback) => {
		_events[event].push(callback);
	};

	const unregisterCallback = (event, callback) => {
		const index = _events[event].indexOf(callback);
		if (index < 0) return;
		_events[event].splice(index, 1);
	};

	const start = () => {
		switch (_playbackMode) {
			case PB_VIDEO:
				vologram.attachedVideo.currentTime = 0;
				vologram.attachedVideo.loop = true;
				vologram.attachedVideo.play();
				break;
			case PB_AUDIO:
				vologram.attachedAudio.currentTime = 0;
				vologram.attachedAudio.loop = false;
				vologram.attachedAudio.play();
			default:
				break;
		}
		_startTimer();
	};

	const pause = () => {
		switch (_playbackMode) {
			case PB_VIDEO:
				vologram.attachedVideo.pause();
				vologram.attachedVideo.loop = false;
				break;
			case PB_AUDIO:
				vologram.attachedAudio.pause();
				vologram.attachedAudio.loop = false;
				break;
			default:
				break;
		}
		_timerPaused = true;
	};

	const play = () => {
		switch (_playbackMode) {
			case PB_VIDEO:
				vologram.attachedVideo.currentTime = 0;
				vologram.attachedVideo.loop = true;
				break;
			case PB_AUDIO:
				vologram.attachedAudio.loop = false;
				vologram.attachedAudio.play();
				break;
			default:
				break;
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

	const getMediaPlayer = () => {
		if (_playbackMode === PB_AUDIO) return _audioPlayer;
		if (_playbackMode === PB_VIDEO) return _videoPlayerElem;
		else return null;
	};

	return {
		get vologram() {
			return vologram;
		},
		start,
		pause,
		play,
		isPlaying,
		mute,
		open,
		close,
		registerCallback,
		unregisterCallback,
		get extensions() {
			return _extensionExports;
		},
	};
};