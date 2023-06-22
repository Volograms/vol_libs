/** @typedef {import('../types/VologramPlayer').VologramPlayerExtensionConstructor} VologramPlayerExtensionConstructor */

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

/** @type {VologramPlayerExtensionConstructor} */
const ThreeJsPlayerExtension = (options) => {
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

	const _init = (inVologram) => {
		vologram = inVologram;
		const fmts = vologram.find_basis_fmt(canvas.getContext("webgl"));
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
			console.log(vologram.attachedVideo);
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

	const _updateMesh = () => {
		// It seems that calculating bounding sphere does not work
		// and always returns NaN value for the radius
		if (objs.geometry.boundingSphere === null) {
			objs.geometry.boundingSphere = new three.Sphere();
		}
		if (objs.geometry.boundingSphere.radius <= 0 || Number.isNaN(objs.geometry.boundingSphere.radius)) {
			objs.geometry.computeBoundingBox();
			objs.geometry.boundingBox.getBoundingSphere(objs.geometry.boundingSphere);
		}
		objs.geometry.needsUpdate = true;
		objs.mesh.needsUpdate = true;
		return true;
	};

	const _updateBasisTexture = () => {
		if (!vologram.run_basis_transcode(_basisFmt)) return false;
		const texData = vologram.basis_get_data();
		objs.texture.mipmaps[0].data.set(texData);
		objs.texture.minFilter = three.LinearFilter;
		objs.texture.needsUpdate = true;
		return true;
	};

	const _update = () => {
		if (!_updateMesh()) return false;
		if (vologram.header.hasBasisTexture) return _updateBasisTexture();
		// else { Video texture updated automatically }
		return true;
	};

	const _renderUpdate = () => {
		if (!_update() || !objs.geometry) {
			return false;
		}

		// Positions - fetch and upload.\
		objs.geometry.setAttribute("position", new three.Float32BufferAttribute(vologram.frame.positions, 3));

		if (vologram.header.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			objs.geometry.setAttribute("normal", new three.Float32BufferAttribute(vologram.frame.normals, 3));
		}

		// Texture Coordinates - fetch and upload.
		objs.geometry.setAttribute("uv", new three.Float32BufferAttribute(vologram.frame.texCoords, 2));

		// Indices - fetch and upload.
		objs.geometry.setIndex(new three.Uint16BufferAttribute(vologram.frame.indices, 1));
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
