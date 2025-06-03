const VologramPlayer = (extensions) => {
	extensions = extensions || [];
	const _extensionExports = {};
	let _wasm = {};
	let _frameRequestId;
	let _frameFromTime = 0;
	let _timerPaused;
	let _timerLooping;
	let _previousTime;
	let _timer = 0;
	let vologram = {};

	const PB_TIMER = 0;
	const PB_VIDEO = 1;
	const PB_AUDIO = 2;

	let _playbackMode = PB_TIMER;

	const _events = {
		onclose: [],
		onended: [],
	};

	const _loadMesh = (frameIdx) => {
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

	const _updateMeshFrameAllowingSkip = (desiredFrameIndex) => {
		if (vologram.lastFrameLoaded === desiredFrameIndex) {
			return false;
		} // Safety catch to avoid reloading the same frame twice.
		if (desiredFrameIndex < 0 || desiredFrameIndex >= vologram.header.frameCount) {
			return false;
		}

		let keyframeRequired = vologram.find_previous_keyframe(desiredFrameIndex);
		if(keyframeRequired === -1) { 
			// We need to update frame directory 
			if(vologram.update_frames_directory(desiredFrameIndex) === false) {
				return false;
			}
			keyframeRequired = vologram.find_previous_keyframe(desiredFrameIndex);
			if(keyframeRequired === -1) {
				_pause();
				return false;
			}
		}
		// If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
		if (vologram.lastKeyframeLoaded !== keyframeRequired  && keyframeRequired !== desiredFrameIndex) {
			if (!_loadMesh(keyframeRequired)) {
				return false;
			}
		}
		// Load actual current frame.
		const ret = _loadMesh(desiredFrameIndex);
		return true;
	};

	const _initVologram = () => {
		let ret = false;

		// Determine file paths based on storage mode
		const basePath = vologram.useOPFS ? '/opfs/' : '';
		
		if (vologram.header.singleFile) {
			const vologramPath = basePath + "vologram.vols";
			console.log(`Creating single file info for: ${vologramPath}`);
			ret = vologram.create_single_file_info(vologramPath);
		} else {
			const headerPath = basePath + "header.vols";
			const sequencePath = basePath + "sequence.vols";
			console.log(`Creating file info for: ${headerPath}, ${sequencePath}`);
			ret = vologram.create_file_info(headerPath, sequencePath);
		}

		if (!ret) {
			console.error("failed to load vologram");
			return false;
		}

		_initVologramHeader();
		_initAdditionalElements();
		_initPlayerExtensions();

		return true;
	};

	const _initVologramHeader = () => {
		vologram.header.hasNormals = vologram.has_normals();
		vologram.header.hasTexture = vologram.has_texture();

		vologram.header.hasAudio = vologram.has_audio();

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
	};

	const _initAdditionalElements = () => {
		if (!vologram.textureUrl && vologram.attachedVideo) {
			vologram.attachedVideo = null;
		}
		if (!vologram.header.hasTexture && vologram.textureUrl && !vologram.attachedVideo) {
			_createVideo();
		}
		if (!vologram.header.hasAudio && vologram.attachedAudio) {
			vologram.attachedAudio = null;
		}
		if (vologram.header.hasAudio && !vologram.attachedAudio) {
			_createAudio();
		}
		if (vologram.header.hasAudio) {
			_getAudioData();
		}

		if (vologram.attachedVideo) {
			_playbackMode = PB_VIDEO;
		} else if (vologram.attachedAudio) {
			_playbackMode = PB_AUDIO;
		} else {
			_playbackMode = PB_TIMER;
			_frameRequestId = requestAnimationFrame(_updateFrameFromTimer);
		}
	};

	const _initPlayerExtensions = () => {
		// Initialise Extensions e.g. ThreeJsPlayer or WebGlPlayer
		extensions.forEach((ext) => {
			ext.init(vologram);
			_extensionExports[ext.name] = ext.exports;
		});
	};

	const _initWasmSingleFile = async (onProgress) =>
		VolWeb()
			.then(async (wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				
				// Initialize OPFS if requested and available
				if (vologram.useOPFS) {
					console.log('Vologram storage mode: OPFS (disk-based)');
					console.log('Initializing OPFS for vologram storage...');
					const opfsInit = await _wasm.initOPFS();
					if (opfsInit) {
						const opfsSetup = await _wasm.setupOPFS();
						if (!opfsSetup) {
							console.warn('Failed to setup OPFS, falling back to MEMFS');
							vologram.useOPFS = false;
						}
					} else {
						console.warn('OPFS not available, falling back to MEMFS');
						vologram.useOPFS = false;
					}
				}
				
				if (!vologram.useOPFS) {
					console.log('Vologram storage mode: MEMFS (memory-based)');
				}
				
				// Use appropriate file paths and fetch function based on storage mode
				const volumeFile = vologram.useOPFS ? "/opfs/vologram.vols" : "vologram.vols";
				
				if (vologram.useOPFS) {
					console.log('Using WasmFS OPFS storage for vologram file');
					return _wasm.fetch_stream_file(volumeFile, vologram.sequenceUrl, onProgress);
				} else {
					return _wasm.fetch_stream_file(volumeFile, vologram.sequenceUrl, onProgress);
				}
			})
			.then((response) => {
				return new Promise(async (resolve, reject) => {
					// Wait until we have a header and audio donwloaded
					await _wasm.isHeaderLoaded();
					const initSuccess = _initVologram();
					if (initSuccess) resolve(initSuccess);
					else reject(new Error("_initVologram failed to open vologram"));
				});
			})
			.catch((err) => {
				console.error(err);
				return false;
			});

	const _initWasm = (onProgress) =>
		VolWeb()
			.then(async (wasmInstance) => {
				_wasm = wasmInstance;
				_wasm.ccall("basis_init", "boolean");
				_wasm.initVologramFunctions(vologram);
				
				// Initialize OPFS if requested and available
				if (vologram.useOPFS) {
					console.log('Vologram storage mode: OPFS (disk-based)');
					console.log('Initializing OPFS for vologram storage...');
					const opfsInit = await _wasm.initOPFS();
					if (opfsInit) {
						const opfsSetup = await _wasm.setupOPFS();
						if (!opfsSetup) {
							console.warn('Failed to setup OPFS, falling back to MEMFS');
							vologram.useOPFS = false;
						}
					} else {
						console.warn('OPFS not available, falling back to MEMFS');
						vologram.useOPFS = false;
					}
				}
				
				if (!vologram.useOPFS) {
					console.log('Vologram storage mode: MEMFS (memory-based)');
				}
				
				// Use appropriate file paths based on storage mode
				const headerFile = vologram.useOPFS ? "/opfs/header.vols" : "header.vols";
				
				if (vologram.useOPFS) {
					console.log('Using WasmFS OPFS storage for vologram files');
				}
				
				return _wasm.fetch_file(headerFile, vologram.headerUrl, onProgress);
			})
			.then((response) => {
				// Fetch sequence file with the same storage mode
				const sequenceFile = vologram.useOPFS ? "/opfs/sequence.vols" : "sequence.vols";
				return _wasm.fetch_file(sequenceFile, vologram.sequenceUrl, onProgress);
			})
			.then((response) => {
				return new Promise((resolve, reject) => {
					const initSuccess = _initVologram();
					if (initSuccess) resolve(initSuccess);
					else reject(new Error("_initVologram failed to open vologram"));
				});
			})
			.catch((err) => {
				console.error(err);
				return false;
			});

	const _getFrameFromSeconds = (seconds) => {
		_frameFromTime = Math.floor(seconds * vologram.header.fps);
	};

	const _timeTick = (nowTimestamp) => {
		if (_timerPaused) {
			_previousTime = nowTimestamp;
			return;
		}
		const delta = Math.max(0, nowTimestamp - _previousTime);
		_timer += delta;
		_previousTime = nowTimestamp;

		_getFrameFromSeconds(_timer / 1000);
		if (_frameFromTime >= vologram.header.frameCount) {
			_events.onended.forEach((fn) => fn());
			if (_timerLooping) {
				_frameFromTime = 0;
				_timer = 0;
			} else {
				_frameFromTime = vologram.header.frameCount - 1;
				_pause();
			}
		}
	};

	const _startTimer = () => {
		_previousTime = performance.now();
		_timerPaused = false;
		_frameFromTime = 0;
		_timer = 0;
	};

	const _updateFrameFromVideo = (now, metadata) => {
		if (vologram.header && vologram.header.ready && vologram.attachedVideo) {
			_getFrameFromSeconds(metadata.mediaTime);
			_updateMeshFrameAllowingSkip(_frameFromTime);
			vologram.lastUpdateTime = metadata.mediaTime;
		}
		_frameRequestId = vologram.attachedVideo.requestVideoFrameCallback(_updateFrameFromVideo); // Re-register the callback to be notified about the next frame.
	};

	const _updateFrameFromTimer = (now) => {
		_timeTick(now);
		if (!_timerPaused && vologram.header && vologram.header.ready) {
			_updateMeshFrameAllowingSkip(_frameFromTime);
			vologram.lastUpdateTime = _timer / 1000;
		}
		_frameRequestId = requestAnimationFrame(_updateFrameFromTimer);
	};

	const _updateFrameFromAudio = () => {
		if (vologram.header && vologram.header.ready && vologram.attachedAudio) {
			_getFrameFromSeconds(vologram.attachedAudio.currentTime);
			_updateMeshFrameAllowingSkip(Math.max(0, _frameFromTime - 1));
			vologram.lastUpdateTime = vologram.attachedAudio.currentTime;
		}
		if (vologram.attachedAudio) _frameRequestId = requestAnimationFrame(_updateFrameFromAudio);
	};

	const _attachVideo = (videoElement) => {
		videoElement.onended = (e) => {
			_events.onended.forEach((fn) => fn());
		};
		vologram.attachedVideo = videoElement;
		videoElement.src = vologram.textureUrl;
		_frameRequestId = videoElement.requestVideoFrameCallback(_updateFrameFromVideo);
	};

	const _createVideo = () => {
		const videoElmnt = document.createElement("video");
		document.body.insertAdjacentElement("afterbegin", videoElmnt);
		videoElmnt.hidden = true;
		videoElmnt.createdByVologramsPlayer = true;
		_attachVideo(videoElmnt);
	};

	const _attachAudio = (audioElement) => {
		audioElement.addEventListener("ended", () => {
			_events.onended.forEach((fn) => fn());
		});
		vologram.attachedAudio = audioElement;
		_frameRequestId = requestAnimationFrame(_updateFrameFromAudio);
	};

	const _createAudio = () => {
		if (!vologram.header.hasAudio) {
			console.warn("Vologram has no audio - cannot create audio element");
			return;
		}
		const audioElmnt = document.createElement("audio");
		document.body.insertAdjacentElement("afterbegin", audioElmnt);
		audioElmnt.hidden = true;
		audioElmnt.createdByVologramsPlayer = true;
		_attachAudio(audioElmnt);
	};

	const _getAudioData = () => {
		const blob = new Blob([vologram.get_audio_data()], { type: "audio/mpeg" });
		const blobUrl = URL.createObjectURL(blob);
		window.addEventListener(
			"beforeunload",
			(e) => {
				URL.revokeObjectURL(blobUrl);
			},
			false
		);
		vologram.attachedAudio.src = blobUrl;
	};

	const _open = async ({ headerUrl, sequenceUrl, textureUrl, videoElement, audioElement, useOPFS = false }, onProgress) => {
		vologram = {};
		vologram.header = {};
		vologram.frame = {};
		vologram.header.singleFile = !headerUrl;
		vologram.headerUrl = headerUrl;
		vologram.sequenceUrl = sequenceUrl;
		vologram.textureUrl = textureUrl;
		
		// Store OPFS preference
		vologram.useOPFS = useOPFS;
		console.log(`Vologram storage mode: ${useOPFS ? 'OPFS (disk-based)' : 'MEMFS (memory-based)'}`);
		
		if (videoElement && audioElement) {
			console.warn("Using both video and audio elements is not supported, audio element will be ignored");
		}
		if (videoElement) _attachVideo(videoElement);
		else if (audioElement) _attachAudio(audioElement);

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

	const _close = () => {
		if (_frameRequestId && !vologram.attachedVideo) cancelAnimationFrame(_frameRequestId);

		_timerPaused = true;
		_timerLooping = false;
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
		_playbackMode = PB_TIMER;
	};

	const _registerCallback = (event, callback) => {
		if (!_events[event]) {
			console.warn(`Vologram player event ${event} does not exist`);
			return;
		}
		_events[event].push(callback);
	};

	const _unregisterCallback = (event, callback) => {
		if (!_events[event]) {
			console.warn(`Vologram player event ${event} does not exist`);
			return;
		}
		const index = _events[event].indexOf(callback);
		if (index < 0) {
			console.warn(`Inputted callback does not exist in event ${event}`);
			return;
		}
		_events[event].splice(index, 1);
	};

	const _start = () => {
		switch (_playbackMode) {
			case PB_VIDEO:
				vologram.attachedVideo.currentTime = 0;
				vologram.attachedVideo.play();
				break;
			case PB_AUDIO:
				vologram.attachedAudio.currentTime = 0;
				vologram.attachedAudio.play();
			default:
				break;
		}
		_startTimer();
	};

	const _pause = () => {
		switch (_playbackMode) {
			case PB_VIDEO:
				vologram.attachedVideo.pause();
				break;
			case PB_AUDIO:
				vologram.attachedAudio.pause();
				break;
			default:
				break;
		}
		_timerPaused = true;
	};

	const _play = () => {
		switch (_playbackMode) {
			case PB_VIDEO:
				vologram.attachedVideo.play();
				break;
			case PB_AUDIO:
				vologram.attachedAudio.play();
				break;
			default:
				if (_frameFromTime >= vologram.header.frameCount - 1) {
					_startTimer();
				}
				break;
		}
		_timerPaused = false;
	};

	const _isPlaying = () => {
		if (vologram.attachedVideo) return !vologram.attachedVideo.paused && !vologram.attachedVideo.ended;
		if (vologram.attachedAudio) return !vologram.attachedAudio.paused && !vologram.attachedAudio.ended;
		else return !_timerPaused;
	};

	const _setMute = (setValue) => {
		if (vologram.attachedVideo) vologram.attachedVideo.muted = setValue;
		if (vologram.attachedAudio) vologram.attachedAudio.muted = setValue;
	};

	const _getMute = () => {
		if (vologram.attachedVideo) return vologram.attachedVideo.muted;
		if (vologram.attachedAudio) return vologram.attachedAudio.muted;
		return false;
	};

	const _setLoop = (newValue) => {
		if (vologram.attachedVideo) vologram.attachedVideo.loop = newValue;
		if (vologram.attachedAudio) vologram.attachedAudio.loop = newValue;
		_timerLooping = newValue;
	};

	const _getLoop = () => {
		if (vologram.attachedVideo) return vologram.attachedVideo.loop;
		if (vologram.attachedAudio) return vologram.attachedAudio.loop;
		return _timerLooping;
	};

	const getMediaPlayer = () => {
		if (_playbackMode === PB_AUDIO) return vologram.attachedAudio;
		if (_playbackMode === PB_VIDEO) return vologram.attachedVideo;
		else return null;
	};

	return {
		get vologram() {
			return vologram;
		},
		get start() {
			return _start;
		},
		get pause() {
			return _pause;
		},
		get play() {
			return _play;
		},
		get isPlaying() {
			return _isPlaying;
		},
		get mute() {
			return _getMute();
		},
		set mute(newValue) {
			return _setMute(newValue);
		},
		get open() {
			return _open;
		},
		get close() {
			return _close;
		},
		get loop() {
			return _getLoop();
		},
		set loop(newValue) {
			_setLoop(newValue);
		},
		get registerCallback() {
			return _registerCallback;
		},
		get unregisterCallback() {
			return _unregisterCallback;
		},
		get extensions() {
			return _extensionExports;
		},
	};
};
