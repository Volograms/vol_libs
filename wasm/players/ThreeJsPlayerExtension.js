const BASIS_VERT = /*glsl*/ `
varying vec2 rev_uv;
void main () {
	rev_uv = vec2(uv.x, 1.0 - uv.y);
	gl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );
}
`;

const BASIS_FRAG = /*glsl*/ `
uniform sampler2D map;
varying vec2 rev_uv;
void main () {
	vec4 color = texture2D( map, rev_uv );
	gl_FragColor = color;
}
`;

const ThreeJsPlayerExtension = (glCtx, options) => {
	if (!glCtx) {
		console.error("Volograms ThreeJsPlayerExtension requires a gl rendering context");
		return;
	}

	// Process input options
	const three = THREE || (options?.threeModule ?? null);

	if (!three) {
		console.error("THREE module cannot be found");
		return;
	}

	const objs = {};

	let _glFmt;
	let _basisFmt;
	let _callbackId;
	let vologram;
	let _cachedTexture = null;
	let _cachedMesh = null;
	let _lastLoadedFrame = -1;
	let _positions = null;
	let _normals = null;
	let _uvs = null;
	let _indices = null;
	let _frameRequestId;

	const _init = (inVologram) => {
		vologram = inVologram;
		const fmts = vologram.find_basis_fmt(glCtx);
		_glFmt = fmts[0];
		_basisFmt = fmts[1];
		_createThreeJsVologram();
	};

	const _createScene = () => {
		objs.scene = new three.Scene();
	};

	const _createCamera = (canvas) => {
		objs.camera = new three.PerspectiveCamera(75, canvas.clientWidth / canvas.clientHeight, 0.1, 1000);
		objs.scene.add(objs.camera);
	};

	const _createRenderer = (canvas) => {
		objs.renderer = new three.WebGLRenderer({ canvas: canvas });
		objs.renderer.setSize(canvas.clientWidth, canvas.clientHeight);
		objs.renderer.setClearColor(new three.Color(147.0 / 255.0, 149.0 / 255.0, 237.0 / 255.0), 1.0);
	};

	const _createThreeJsScene = (canvas) => {
		_createScene();
		_createCamera(canvas);
		_createRenderer(canvas);

		objs.scene.add(objs.mesh);
	};

	const _createVologramGeometry = () => {
		objs.geometry = new three.BufferGeometry();
	};

	const _getFrameFromSeconds = (seconds) => {
		return Math.round(seconds * vologram.header.fps);
	};

	const _createVologramTexture = () => {
		if (!vologram.header.hasBasisTexture) {
			_videoTexture = new three.VideoTexture(vologram.attachedVideo);
			
			// Create a placeholder DataTexture. It will be resized and updated with video frames later.
			const placeholderPixels = new Uint8Array([0, 0, 0, 255]); // A single black pixel
			objs.texture = new three.DataTexture(placeholderPixels, 1, 1, three.RGBAFormat);
			objs.texture.minFilter = three.LinearFilter;
			objs.texture.magFilter = three.LinearFilter;
			objs.texture.flipY = true; // Videos need to be flipped vertically to match WebGL coordinates.
			objs.texture.needsUpdate = true;
			
			_frameRequestId = vologram.attachedVideo.requestVideoFrameCallback(_updateFrameFromVideo);
		} else {
		const texDataSize = vologram.header.textureWidth * vologram.header.textureHeight;
		const data = new Uint8Array(texDataSize);
		objs.texture = new three.CompressedTexture(
			[{ data, width: vologram.header.textureWidth, height: vologram.header.textureHeight }],
			vologram.header.textureWidth,
			vologram.header.textureHeight,
			_glFmt,
			three.UnsignedByteType
		);
		objs.texture.minFilter = three.LinearFilter;
		objs.texture.needsUpdate = true;
		}
	};

	const _createVologramMesh = () => {
		objs.mesh = new three.Mesh(objs.geometry);
		objs.mesh.frustumCulled = false;
		objs.mesh.needsUpdate = true;
	};

	const _createVologramMaterial = () => {
		objs.material = vologram.header.hasBasisTexture
			? new three.ShaderMaterial({
					vertexShader: BASIS_VERT,
					fragmentShader: BASIS_FRAG,
					uniforms: {
						map: { value: objs.texture },
					},
			  })
			: new three.MeshBasicMaterial({
					map: objs.texture,
			  });
		objs.material.needsUpdate = true;
		objs.mesh.material = objs.material;
		objs.mesh.needsUpdate = true;
	};

	const _createThreeJsVologram = () => {
		// It is important to create the geometry and texture before the mesh
		_createVologramGeometry();
		_createVologramTexture();
		_createVologramMesh();
		_createVologramMaterial();
	};

	const _updateMeshBoundingBox = () => {
		if (!objs || !objs.geometry || !objs.mesh) return false;
		// It seems that calculating bounding sphere does not work
		// and always returns NaN value for the radius
		if (objs.geometry.boundingSphere === null) {
			objs.geometry.boundingSphere = new three.Sphere();
		}
		if (objs.geometry.boundingSphere.radius <= 0 || Number.isNaN(objs.geometry.boundingSphere.radius)) {
			objs.geometry.computeBoundingBox();
			objs.geometry.boundingBox.getBoundingSphere(objs.geometry.boundingSphere);
		}
		return true;
	};

	const _updateMeshAttributeArrays = () => {
		_positions = new three.Float32BufferAttribute(vologram.frame.positions, 3);
		if (vologram.header.hasNormals) {
			_normals = new three.Float32BufferAttribute(vologram.frame.normals, 3);
		}
		_uvs = new three.Float32BufferAttribute(vologram.frame.texCoords, 2);
		_indices = new three.Uint16BufferAttribute(vologram.frame.indices, 1)
	}

	const _updateBasisTexture = () => {
		if (!vologram.run_basis_transcode(_basisFmt)) return false;
		_cachedMesh = {
			positions: _positions,
			normals: _normals,
			texCoords: _uvs,
			indices: _indices,
			frameIndex: vologram.lastFrameLoaded
		};
		const texData = vologram.basis_get_data();
		objs.texture.mipmaps[0].data.set(texData);
		objs.texture.minFilter = three.LinearFilter;
		objs.texture.needsUpdate = true;
		return true;
	};

	const _updateFrameFromVideo = (now, metadata) => {
		const video = vologram.attachedVideo;
		if(!video) return;

		_frameFromTime = _getFrameFromSeconds(metadata.mediaTime);

		// The video dimensions might not be available on the first frame, so we wait.
		if (!metadata.width || !metadata.height) {
			_frameRequestId = vologram.attachedVideo.requestVideoFrameCallback(_updateFrameFromVideo);
			return;
		}

		// Use a 2D canvas to get pixel data from the current video frame.
		// This is a CPU-side operation that avoids needing the WebGL renderer.
		const canvas = document.createElement('canvas');
		canvas.width = video.videoWidth;
		canvas.height = video.videoHeight;
		const ctx = canvas.getContext('2d');
		ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
		const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);

		// Cache the raw pixel data and dimensions.
		_cachedTexture = {
			pixels: imageData.data, // This is a Uint8ClampedArray
			width: canvas.width,
			height: canvas.height,
			frameIndex: _frameFromTime
		};
		
		// load mesh
		_updateMeshAttributeArrays();

		// Cache the corresponding mesh data to ensure they are in sync.
		_cachedMesh = {
			positions: _positions,
			normals: _normals,
			texCoords: _uvs,
			indices: _indices,
			frameIndex: vologram.lastFrameLoaded
		};
		_frameRequestId = vologram.attachedVideo.requestVideoFrameCallback(_updateFrameFromVideo);
	}

	const _renderUpdate = () => {
		// check if we have an existing geometry object
		if (!objs.geometry) return false;

		// skip update for already loaded frame
		if(_lastLoadedFrame === vologram.lastFrameLoaded) return true;
		let frameID = vologram.lastFrameLoaded

		// update bounding box (should that be done after setting the mesh?)
		if (!_updateMeshBoundingBox()) return false;

		// Set texture
		if (vologram.header.hasBasisTexture) {
			// load mesh
			_updateMeshAttributeArrays();

			if(!_updateBasisTexture()) return false;
		}
		else { 
			// Handle non-basis texture (video texture) by applying cached CPU-side frames.
			if (!_cachedTexture || !_cachedMesh) return false;
			
			if (_cachedTexture.frameIndex === _cachedMesh.frameIndex) {
				const { pixels, width, height } = _cachedTexture;
		
				// If texture dimensions are different, we need to create a new texture.
				if (!objs.texture || objs.texture.image.width !== width || objs.texture.image.height !== height) {
					if (objs.texture) {
						objs.texture.dispose();
					}
					const newTexture = new three.DataTexture(pixels, width, height, three.RGBAFormat);
					newTexture.minFilter = three.LinearFilter;
					newTexture.magFilter = three.LinearFilter;
					newTexture.flipY = true; // Videos need to be flipped vertically.
					objs.texture = newTexture;
					// Make sure the material uses the new texture object.
					objs.material.map = newTexture;
				} else {
					// Otherwise, just update the existing texture's data.
					objs.texture.image.data.set(pixels);
				}
				objs.texture.needsUpdate = true;
								
				// Update frame ID.
				frameID = _cachedTexture.frameIndex;
				
				// Clear cache once applied.
				_cachedTexture = null;
			} else {
				// Clear wrong cache if mesh and texture are out of sync.
				_cachedTexture = null;
				return false;
			}
		}
		// Positions - upload.
		objs.geometry.setAttribute("position", _cachedMesh.positions);

		if (vologram.header.hasNormals) {
			// Not all volograms include normals.
			// Normals - upload.
			objs.geometry.setAttribute("normal", _cachedMesh.normals);
		}

		// Texture Coordinates - upload.
		objs.geometry.setAttribute("uv", _cachedMesh.texCoords);

		// Indices - upload.
		objs.geometry.setIndex(_cachedMesh.indices);
		objs.geometry.needsUpdate = true;
		objs.mesh.needsUpdate = true;

		// Save latest frame ID
		_lastLoadedFrame = frameID;
		return true;
	};

	const _animate = () => {
		if (_renderUpdate()) objs.renderer.render(objs.scene, objs.camera);
		_callbackId = requestAnimationFrame(_animate);
	};

	const _close = () => {
		if (_callbackId) cancelAnimationFrame(_callbackId);
		if (objs) {
			objs.mesh = null;
			objs.geometry = null;
			objs.texture?.dispose();
			objs.texture = null;
			objs.material = null;
		}
		if (_frameRequestId && !vologram.attachedVideo) cancelAnimationFrame(_frameRequestId);
	};

	const _setRenderer = (renderer) => {
		objs.renderer = renderer;
	}

	return {
		get name() {
			return "threejs";
		},
		get init() {
			return _init;
		},
		get close() {
			return _close;
		},
		exports: {
			get objects() {
				return objs;
			},
			get renderUpdate() {
				return _renderUpdate;
			},
			get createScene() {
				return _createThreeJsScene;
			},
			get beginRendering() {
				return _animate;
			},
			get setRenderer() {
				return _setRenderer
			}
		},
	};
};
