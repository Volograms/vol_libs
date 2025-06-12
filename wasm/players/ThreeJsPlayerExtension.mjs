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
	let _useWorker = false;
	let _manager = null;
	let _isTranscoding = false;
	let _cachedTexture = null;
	let _cachedMesh = null;
	let _lastLoadedFrame = -1;
	let _positions = null;
	let _normals = null;
	let _uvs = null;
	let _indices = null;


	const _init = (inVologram, { useWorker, manager } = {}) => {
		vologram = inVologram;
		_useWorker = useWorker;
		_manager = manager;
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

	const _createVologramTexture = () => {
		if (!vologram.header.hasBasisTexture) {
			objs.texture = new three.VideoTexture(vologram.attachedVideo);
			return;
		}

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

	const _updateMeshBS = () => {
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

	const _updateBasisTexture = () => {
		if (_useWorker) {
			if (_isTranscoding) {
				return false; // Don't send a new request if one is already in flight.
			}

			const ptr = vologram.frame_texture_data_ptr();
			const size = vologram.frame_texture_sz();
			if (!ptr || !size) { return false; }
			const basisData = new Uint8Array(vologram.HEAP8.buffer, ptr, size).slice();
			const frameIndex = vologram.lastFrameLoaded;
			_isTranscoding = true;

			const cachedMesh = {
				positions: _positions,
				normals: _normals,
				texCoords: _uvs,
				indices: _indices,
				frameIndex: frameIndex
			};
			
			_manager.transcode(basisData, _basisFmt, frameIndex)
				.then(({ transcodedData, width, height, frameIndex: transcodeFrameIndex }) => {
					// Store the result in the cache.
					if (cachedMesh && cachedMesh.frameIndex > frameIndex) return true;
					_cachedTexture = {
						data: transcodedData,
						width,
						height,
						frameIndex: transcodeFrameIndex
					};
					_cachedMesh = cachedMesh;
					_isTranscoding = false; // Allow new requests to be sent.
					return true;
				})
				.catch(err => {
					console.error("Vologram transcoding error:", err);
					_isTranscoding = false; // Reset on error as well.
					return false;
				});
		} else {
			if (!vologram.run_basis_transcode(_basisFmt)) { return false; }
			const texData = vologram.basis_get_data();
			objs.texture.mipmaps[0].data.set(texData);
			objs.texture.minFilter = three.LinearFilter;
			objs.texture.needsUpdate = true;
		}
		return true;
	};

	const _renderUpdate = () => {
		// check if we have an existing geometry object
		if (!objs.geometry) return false;

		// skip update for already loaded frame
		if(_lastLoadedFrame === vologram.lastFrameLoaded) return true;
		let frameID = vologram.lastFrameLoaded

		// update bounding box (should that be done after setting the mesh?)
		if (!_updateMeshBS()) return false;

		// Get mesh data for current frame to keep it in sync with its texture.
		if(!_useWorker || !_isTranscoding) {
			_positions = new three.Float32BufferAttribute(vologram.frame.positions, 3);
			if (vologram.header.hasNormals) {
				_normals = new three.Float32BufferAttribute(vologram.frame.normals, 3);
			}
			_uvs = new three.Float32BufferAttribute(vologram.frame.texCoords, 2);
			_indices = new three.Uint16BufferAttribute(vologram.frame.indices, 1)
		}
		
		// Load texture
		if (vologram.header.hasBasisTexture) {
			if(!_updateBasisTexture()) return false;

			// Check cache and apply texture if it's ready and matches the current frame.
			if (_useWorker) {
				if(!_cachedTexture) return false;

				if(_cachedTexture.frameIndex === _cachedMesh.frameIndex) { // this should always be the case
					// console.log(_cachedTexture.frameIndex)
					
					const { data, width, height } = _cachedTexture;
					if (objs.texture.image.width !== width || objs.texture.image.height !== height) {
						objs.texture.image.width = width;
						objs.texture.image.height = height;
					}
					objs.texture.mipmaps[0].data.set(data);
					objs.texture.minFilter = three.LinearFilter;
					objs.texture.needsUpdate = true;
					
					// set correct mesh data
					_positions = _cachedMesh.positions;
					_normals = _cachedMesh.normals;
					_uvs = _cachedMesh.texCoords;
					_indices = _cachedMesh.indices;

					// update the frame ID we are going to apply
					frameID = _cachedTexture.frameIndex;

					// Clear cache once applied.
					_cachedTexture = null; 
					_cachedMesh = null

				} else {
					// Clear wrong cache
					_cachedTexture = null; 
					_cachedMesh = null;
					return false;
				}
			}
			// else {} // loading texture in the main thread is blockig and we don't need to use cache
		}
		// else { Video texture updated automatically }

		// Positions - upload.
		objs.geometry.setAttribute("position", _positions);

		if (vologram.header.hasNormals) {
			// Not all volograms include normals.
			// Normals - upload.
			objs.geometry.setAttribute("normal", _normals);
		}

		// Texture Coordinates - upload.
		objs.geometry.setAttribute("uv", _uvs);

		// Indices - upload.
		objs.geometry.setIndex(_indices);
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
	};

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
		},
	};
};

export default ThreeJsPlayerExtension;
