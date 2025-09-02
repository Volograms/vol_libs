const RESUME_BUFFER_SECONDS = 2.0;

const VologramPlayer = (extensions) => {
	extensions = extensions || [];
	const _extensionExports = {};
	let _wasm = {};
	let _transcoderManager;
	let _useWorker = false;
	let _frameRequestId;
	let _frameFromTime = 0;
	let _timerPaused;
	let _timerLooping;
	let _previousTime;
	let _timer = 0;
	let vologram = {};
	let _isBuffering = false;
	let _wasPlayingBeforeBuffering = false;

	// AbortController for canceling fetch requests
	let _downloadController = null;

	const PB_TIMER = 0;
	const PB_VIDEO = 1;
	const PB_AUDIO = 2;

	let _playbackMode = PB_TIMER;

	const _events = {
		onclose: [],
		onended: [],
		onbuffering: [],
	};

	const _loadMesh = (frameIdx) => {
		let ret;
		
		// Use appropriate frame reading method based on streaming mode
		if (vologram.isStreamingMode) {
			// In streaming mode, check if frame is available first
			if (!vologram.is_frame_available_in_buffer(frameIdx)) {
				// Frame not available in buffer yet
				console.log(`Frame ${frameIdx} not available in buffer yet`);
				return false;
			}
			
			// Use streaming frame reader
			ret = vologram.read_frame_streaming(frameIdx);
		} else {
			// Use traditional frame reader for full download mode
			ret = vologram.read_frame(frameIdx);
		}

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

	// Unified buffer health monitoring and download control
	const _updateBufferHealth = (currentFrame, enableLogging = false) => {
		if (!vologram.isStreamingMode || !vologram._downloadManager) {
			return; // Not in streaming mode or no download manager
		}

		// Update current frame position for download manager
		vologram._downloadManager.setCurrentFrame(currentFrame);
		
		// Check if we should resume download based on buffer health
		const fps = vologram.header.fps || 30.0;
		if (vologram.should_resume_download(currentFrame, fps)) {
			vologram._downloadManager.resumeDownload();
			
			// Optional debug logging for buffer health
			if (enableLogging) {
				const bufferHealthSeconds = vologram.get_buffer_health_seconds(fps);
				if (bufferHealthSeconds < 1.0) {
					console.log(`Low buffer: ${bufferHealthSeconds.toFixed(1)}s remaining`);
				}
			}
		}
	};

	const _updateMeshFrameAllowingSkip = (desiredFrameIndex) => {
		if (vologram.lastFrameLoaded === desiredFrameIndex && !_isBuffering) {
			return false;
		} // Safety catch to avoid reloading the same frame twice.
		if (desiredFrameIndex < 0 || desiredFrameIndex >= vologram.header.frameCount) {
			return false;
		}

		const bufferGoalFrame = Math.min(
			vologram.header.frameCount - 1,
			desiredFrameIndex + Math.floor(RESUME_BUFFER_SECONDS * vologram.header.fps)
		);

		// Always try to update the directory to our buffer goal if buffering, otherwise just for the current frame.
		const frameReady = vologram.update_frames_directory(_isBuffering ? bufferGoalFrame : desiredFrameIndex);
		if (!frameReady) return false;

		// Check if the frame we absolutely need right now is available.
		let keyframeRequired = vologram.find_previous_keyframe(desiredFrameIndex);

		if (keyframeRequired === -1) {
			// Frame not ready, enter/stay in buffering state.
			if (!_isBuffering) {
				_isBuffering = true;
				_wasPlayingBeforeBuffering = _isPlaying();
				_events.onbuffering.forEach((fn) => fn(true));
			}
			_internal_pause();
			return false;
		}

		// If we were buffering, check if we've met the goal to resume.
		if (_isBuffering) {
			const keyframeForBufferGoal = vologram.find_previous_keyframe(bufferGoalFrame);
			if (keyframeForBufferGoal !== -1) {
				// Buffer goal is met. Exit buffering state.
				_isBuffering = false;
				_events.onbuffering.forEach((fn) => fn(false));
				if (_wasPlayingBeforeBuffering) {
					_play(); // This will set _timerPaused = false
				}
			} else {
				// Goal not met. Stay paused and buffering.
				return false;
			}
		}

		// If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
		if (vologram.lastKeyframeLoaded !== keyframeRequired && keyframeRequired !== desiredFrameIndex) {
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

		if (vologram.header.singleFile) {
			ret = vologram.create_single_file_info("vologram.vols");
		} else {
			ret = vologram.create_file_info("header.vols", "sequence.vols");
		}

		if (!ret) {
			console.error("failed to load vologram");
			return false;
		}

		_initVologramHeader();
		_initAdditionalElements();
		_initPlayerExtensions();

		// Initialize mesh to frame 0
		_updateMeshFrameAllowingSkip(0);

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
			ext.init(vologram, { useWorker: _useWorker, manager: _transcoderManager });
			_extensionExports[ext.name] = ext.exports;
		});
	};

	const _initWasmSingleFile = async (onProgress) => {
		// Create AbortController for this download session
		_downloadController = new AbortController();
		const signal = _downloadController.signal;

		return VolWeb()
			.then(async (wasmInstance) => {
				// Check if download was already cancelled
				if (signal.aborted) return false;

				_wasm = wasmInstance;
				if (!_useWorker) {
					_wasm.ccall("basis_init", "boolean");
				}
				_wasm.initVologramFunctions(vologram);

				// Initialize streaming configuration with enhanced buffer settings
				_wasm.init_streaming_config();
				
				// Optional: Allow custom configuration via vologram options
				if (vologram.streamingConfig) {
					if (vologram.streamingConfig.maxBufferSize) {
						_wasm.set_max_buffer_size(vologram.streamingConfig.maxBufferSize);
					}
					if (vologram.streamingConfig.lookaheadSeconds) {
						_wasm.set_lookahead_seconds(vologram.streamingConfig.lookaheadSeconds);
					}
					if (vologram.streamingConfig.autoSelectMode !== undefined) {
						_wasm.set_auto_select_mode(vologram.streamingConfig.autoSelectMode);
					}
				}

				// Use the enhanced streaming function with circular buffer support
				const downloadManager = _wasm.fetch_stream_buffer("vologram.vols", vologram.sequenceUrl, vologram.streamingConfig, onProgress, signal);

				// Store download manager for buffer management
				vologram._downloadManager = downloadManager;

				// Await for the header to be loaded.
				await downloadManager.headerLoaded;

				// The rest of the download can continue in the background. We can log if it fails.
				downloadManager.downloadFinished.catch((err) => {
					if (err.name !== "AbortError") {
						console.error("Full vologram download failed in background:", err);
					}
				});

				const initSuccess = _initVologram();
				if (initSuccess) {
					// Log which mode we're using
					const isStreaming = downloadManager.isStreamingMode();
					console.log(`Vologram initialized in ${isStreaming ? 'streaming buffer' : 'full download'} mode`);
					
					// Store streaming mode flag
					vologram.isStreamingMode = isStreaming;
					
					return true;
				} else {
					throw new Error("_initVologram failed to open vologram");
				}
			})
			.catch((err) => {
				// Handle cancellation gracefully
				if (err.name === 'AbortError') {
					console.log('Vologram download cancelled by user');
					return false;
				}
				console.error('Vologram download error:', err);
				return false;
			});
	};

	const _initWasm = async (onProgress) => {
		// Create AbortController for this download session
		_downloadController = new AbortController();
		const signal = _downloadController.signal;

		return VolWeb()
			.then(async (wasmInstance) => {
				// Check if download was already cancelled
				if (signal.aborted) return false;

				_wasm = wasmInstance;
				if (!_useWorker) {
					_wasm.ccall("basis_init", "boolean");
				}
				_wasm.initVologramFunctions(vologram);

				await _wasm.fetch_file("header.vols", vologram.headerUrl, onProgress, signal);
			})
			.then(async (response) => {
				if (signal.aborted) return false;

				await _wasm.fetch_file("sequence.vols", vologram.sequenceUrl, onProgress, signal)
			})
			.then((response) => {
				// return new Promise((resolve, reject) => {
				if (signal.aborted) return false;

				const initSuccess = _initVologram();
				if (initSuccess) {
					return true;
				} else {
					throw new Error("_initVologram failed to open vologram");
				}
				// });
			})
			.catch((err) => {
				// Handle cancellation gracefully
				if (err.name === 'AbortError') {
					console.log('Vologram download cancelled by user');
					return false;
				}
				console.error('Vologram download error:', err);
				return false;
			});
	};

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
				_internal_pause();
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
			
			// Unified buffer health monitoring
			_updateBufferHealth(_frameFromTime);
			
			_updateMeshFrameAllowingSkip(_frameFromTime);
			vologram.lastUpdateTime = metadata.mediaTime;
		}
		_frameRequestId = vologram.attachedVideo.requestVideoFrameCallback(_updateFrameFromVideo); // Re-register the callback to be notified about the next frame.
	};

	const _updateFrameFromTimer = (now) => {
		_timeTick(now);
		if ((!_timerPaused || _isBuffering) && vologram.header && vologram.header.ready) {
			// Unified buffer health monitoring (with debug logging enabled for timer mode)
			_updateBufferHealth(_frameFromTime, true);
			
			_updateMeshFrameAllowingSkip(_frameFromTime);
			vologram.lastUpdateTime = _timer / 1000;
		}
		_frameRequestId = requestAnimationFrame(_updateFrameFromTimer);
	};

	const _updateFrameFromAudio = () => {
		if (vologram.header && vologram.header.ready && vologram.attachedAudio) {
			_getFrameFromSeconds(vologram.attachedAudio.currentTime);
			
			// Unified buffer health monitoring (audio uses frame - 1)
			const targetFrame = Math.max(0, _frameFromTime - 1);
			_updateBufferHealth(targetFrame);
			
			_updateMeshFrameAllowingSkip(targetFrame);
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

	const _open = async ({ headerUrl, sequenceUrl, textureUrl, videoElement, audioElement, useWorker = false }, onProgress) => {
		vologram = {};
		vologram.header = {};
		vologram.frame = {};
		vologram.header.singleFile = !headerUrl;
		vologram.headerUrl = headerUrl;
		vologram.sequenceUrl = sequenceUrl;
		vologram.textureUrl = textureUrl;
		_useWorker = useWorker;
		if (_useWorker) {
			if (!_transcoderManager) {
				_transcoderManager = TranscoderManager();
			}
		}
		if (videoElement && audioElement) {
			console.warn("Using both video and audio elements is not supported, audio element will be ignored");
		}
		if (videoElement) _attachVideo(videoElement);
		else if (audioElement) _attachAudio(audioElement);

		if (vologram.header.singleFile) return _initWasmSingleFile(onProgress);
		else return _initWasm(onProgress);
	};

	const _cleanVologramModule = () => {
		
		if (_transcoderManager) {
			_transcoderManager.destroy();
			_transcoderManager = null;
		}
		if (!_useWorker) {
			_wasm.ccall("basis_free", "boolean");
		}
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
		_isBuffering = false;
		_wasPlayingBeforeBuffering = false;
	};

	const _close = () => {
		// Cancel any ongoing downloads first
		if (_downloadController) {
			console.log('Cancelling ongoing vologram download...');
			_downloadController.abort();
			_downloadController = null;
		}

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

	const _internal_pause = () => {
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

	const _pause = () => {
		_wasPlayingBeforeBuffering = false;
		_internal_pause();
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
