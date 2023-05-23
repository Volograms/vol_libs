export default class VologramPlayer {
	#wasm;
	/** @type {number} */ #frameToLoad;
	/** @type {boolean} */ #timerPaused;
	/** @type {number} */ #playbackStartTime;
	vologram = {};

	#events = {
		/** @type {Array<(vologram: any) => void>} */
		onframeready: [],
		/** @type {Array<() => void>} */
		onclose: [],
		/** @type {Array<() => void} */
		onloop: [],
	};

	#loadMesh = (frameIdx) => {
		if (this.vologram.lastFrameLoaded == frameIdx) {
			return;
		} // Safety catch to avoid reloading the same frame twice.

		// Ask the vol_geom WASM to read the frame data from the vologram file into `_frame_data`.
		var ret = this.vologram.read_frame(frameIdx);
		if (!ret) {
			return false;
		}
		this.vologram.frame.isKey = this.vologram.is_keyframe(frameIdx);

		// Positions - fetch and upload.
		this.vologram.frame.positions = this.vologram.frame_get_verts();

		if (this.vologram.header.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			this.vologram.frame.normals = this.vologram.frame_get_norms();
		}

		// Key-Frames also contain texture coordinate and index data.
		if (this.vologram.frame.isKey) {
			this.vologram.lastKeyframeLoaded = frameIdx;
			// Texture Coordinates - fetch and upload.
			this.vologram.frame.texCoords = this.vologram.frame_get_uvs();

			// Indices - fetch and upload.
			this.vologram.frame.indices = this.vologram.frame_get_ind();
		}
		this.vologram.lastFrameLoaded = frameIdx;
		return true;
	};

	// Calls mesh_from_frame() but first loads a keyframe, if required.
	#updateMeshFrameAllowingSkip = (desiredFrameIndex) => {
		var keyframeRequired = this.vologram.find_previous_keyframe(desiredFrameIndex);

		// If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
		if (this.vologram.lastKeyframeLoaded != keyframeRequired) {
			this.#loadMesh(keyframeRequired);
		}
		// Load actual current frame.
		this.#loadMesh(desiredFrameIndex);
	};

	#initVologram = () => {
		var ret = false;
		if (this.vologram.header.singleFile) {
			ret = this.vologram.create_single_file_info("vologram.vols");
		} else {
			ret = this.vologram.create_file_info("header.vols", "sequence.vols");
		}

		console.log("create_file_info=" + ret);
		if (!ret) {
			console.error("failed to load vologram");
			return;
		}

		this.vologram.header.hasNormals = this.vologram.has_normals();
		this.vologram.header.hasTexture = this.vologram.has_texture();
		this.vologram.header.hasAudio = this.vologram.has_audio();
		this.vologram.header.textureCompression = this.vologram.texture_compression();
		this.vologram.header.textureContainerFormat = this.vologram.texture_container_format();
		this.vologram.header.textureWidth = this.vologram.texture_width();
		this.vologram.header.textureHeight = this.vologram.texture_height();
		if (this.vologram.header.textureWidth === 0) this.vologram.header.textureWidth = 2048;
		if (this.vologram.header.textureHeight === 0) this.vologram.header.textureHeight = 2048;
		console.log(this.vologram.header.textureWidth, this.vologram.header.textureHeight);
		this.vologram.header.frameCount = this.vologram.frame_count();
		this.vologram.header.fps = 30;
		this.vologram.header.durationS = this.vologram.header.frameCount / this.vologram.header.fps; // 5.0;
		this.vologram.header.ready = true;
	};

	#progressCheck = (p) => {
		if (Math.floor(p * 100) % 10 === 0) {
			console.log(p * 100 + "%");
		}
	};

	#initWasmSingleFile = async (onProgress) => {
		return VolWeb()
			.then((wasmInstance) => {
				this.#wasm = wasmInstance;
				this.#wasm.ccall("basis_init", "boolean");
				this.#wasm.initVologramFunctions(this.vologram);
				return this.#wasm.fetch_file("vologram.vols", this.vologram.fileUrl, onProgress);
			})
			.then((response) => {
				console.log(response);
				this.#initVologram();
				return true;
			});
	};

	#initWasm = async () => {
		return VolWeb()
			.then((wasmInstance) => {
				this.#wasm = wasmInstance;
				this.#wasm.ccall("basis_init", "boolean");
				this.#wasm.initVologramFunctions(this.vologram);
				return this.#wasm.fetch_file("header.vols", this.vologram.headerUrl, onProgress);
			})
			.then((response) => {
				console.log(response);
				return this.#wasm.fetch_file("sequence.vols", this.vologram.sequenceUrl, onProgress);
			})
			.then((response) => {
				console.log(response);
				this.#initVologram();
				return true;
			});
	};

	#shouldAdvanceFrame = (time) => {
		this.#frameToLoad = Math.floor(this.vologram.header.fps * time);
		if (this.#frameToLoad === this.vologram.lastFrameLoaded) {
			return false;
		}
		if (this.#frameToLoad >= this.vologram.header.frameCount) {
			this.#frameToLoad = 0;
			this.#playbackStartTime = performance.now() / 1000;
			this.#events.onloop.forEach((fn) => fn());
		}
		this.vologram.lastUpdateTime = time;
		return true;
	};

	/** @type {VideoFrameRequestCallback} */
	#videoFrameCallback = (now, metadata) => {
		if (this.vologram.header.ready && this.#shouldAdvanceFrame(metadata.mediaTime)) {
			this.#updateMeshFrameAllowingSkip(this.#frameToLoad);
			this.#events.onframeready.forEach((fn) => fn(this.vologram));
		}
		this.vologram.attachedVideo?.requestVideoFrameCallback(this.#videoFrameCallback);
	};

	/** @type {FrameRequestCallback} */
	#frameRequestCallback = (now) => {
		if (
			!this.#timerPaused &&
			this.vologram.header.ready &&
			this.#shouldAdvanceFrame(now / 1000 - this.#playbackStartTime)
		) {
			this.#updateMeshFrameAllowingSkip(this.#frameToLoad);
			this.#events.onframeready.forEach((fn) => fn(this.vologram));
		}
		if (!this.#timerPaused) requestAnimationFrame(this.#frameRequestCallback);
	};

	play = () => {
		if (this.vologram.attachedVideo) {
			this.vologram.attachedVideo.currentTime = 0;
			this.vologram.attachedVideo.play();
		} else {
			this.#playbackStartTime = performance.now() / 1000;
			this.#timerPaused = false;
			requestAnimationFrame(this.#frameRequestCallback);
		}
	};

	pause = () => {
		if (this.vologram.attachedVideo) this.vologram.attachedVideo.pause();
		else {
			this.#timerPaused = true;
		}
	};

	resume = () => {
		if (this.vologram.attachedVideo) {
			this.vologram.attachedVideo.play();
		} else {
			this.#timerPaused = false;
			requestAnimationFrame(this.#frameRequestCallback);
		}
	};

	isPlaying = () => {
		if (this.vologram.attachedVideo) return !this.vologram.attachedVideo.paused && !this.vologram.attachedVideo.ended;
		else return !this.#timerPaused;
	};

	mute = (setValue) => {
		if (setValue) {
			if (this.vologram.attachedVideo) this.vologram.attachedVideo.muted = setValue;
			// TODO: Audio Element
		} else {
			if (this.vologram.attachedVideo) return this.vologram.attachedVideo.muted;
			// TODO: Audio Element
		}
	};

	/** @type {(videoElement: HTMLVideoElement) => void} */
	attachVideo = (videoElement) => {
		this.vologram.attachedVideo = videoElement;
		videoElement.src = this.vologram.textureUrl;
		videoElement.requestVideoFrameCallback(this.#videoFrameCallback);
	};

	open = async (headerUrl, sequenceUrl, textureUrl, onProgress) => {
		this.vologram = {};
		this.vologram.header = {};
		this.vologram.frame = {};
		this.vologram.header.singleFile = false;
		this.vologram.headerUrl = headerUrl;
		this.vologram.sequenceUrl = sequenceUrl;
		this.vologram.textureUrl = textureUrl;
		return this.#initWasm(onProgress).then((w) => {
			console.log(w);
		});
	};

	openSingleFile = async (fileUrl, onProgress) => {
		this.vologram = {};
		this.vologram.header = {};
		this.vologram.frame = {};
		this.vologram.header.singleFile = true;
		this.vologram.fileUrl = fileUrl;
		return this.#initWasmSingleFile(onProgress).then((w) => {
			console.log(w);
		});
	};

	close = () => {
		this.#timerPaused = true;
		this.vologram.attachedVideo?.pause();
		this.vologram.attachedVideo = null;
		this.#events.onclose.forEach((fn) => fn());
	};

	addEventListener(event, callback) {
		this.#events[event].push(callback);
	}

	removeEventListener(event, callback) {
		let index = this.#events[event].indexOf(callback);
		if (index < 0) return;
		this.#events[event].splice(index, 1);
	}
}
