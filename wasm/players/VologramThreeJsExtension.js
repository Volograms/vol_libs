const vertBasis = /*glsl*/ `
precision mediump float;
varying vec2 v_st;
void main () {
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  v_st = vec2(uv.x, 1.0 - uv.y);
}
`;

const vert = /*glsl*/ `
precision mediump float;
varying vec2 v_st;
void main () {
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  v_st = vec2(uv.x, uv.y);
}
`;

const frag = /*glsl*/ `
precision mediump float;
varying vec2 v_st;
uniform sampler2D tex2d;
void main () {
  vec3 texel_rgb = texture2D( tex2d, v_st ).rgb;
  // discard any magenta fragments.
  if ( texel_rgb.r > 0.9 && texel_rgb.g < 0.17 && texel_rgb.b > 0.9 ) { discard; }
	gl_FragColor = vec4( texel_rgb, 1.0 );
  // gl_FragColor = vec4( v_st.x, v_st.y, 1.0, 1.0 );
}`;

const VologramThreeJsExtension = (renderer3js, basePlayer) => {
	let _vologram;
	let _glFmt;
	let _basisFmt;
	let _basePlayer;
	let _threeObjects;

	let _hasBasisTexture = false;

	const _createVologramGeometry = () => {
		_threeObjects.geometry = new THREE.BufferGeometry();
	};

	const _createVologramTexture = () => {
		if (_vologram.attachedVideo) {
			_threeObjects.texture = new THREE.VideoTexture(_vologram.attachedVideo);
			return;
		}

		const texDataSize = _vologram.header.textureWidth * _vologram.header.textureHeight;
		const data = new Uint8Array(texDataSize);
		_threeObjects.texture = new THREE.CompressedTexture(
			[{ data: data, width: _vologram.header.textureWidth, height: _vologram.header.textureHeight }],
			_vologram.header.textureWidth,
			_vologram.header.textureHeight,
			_glFmt,
			THREE.UnsignedByteType
		);
		_threeObjects.texture.minFilter = THREE.LinearFilter;
		_threeObjects.texture.needsUpdate = true;
	};

	const _createVologramMesh = () => {
		_threeObjects.material = new THREE.ShaderMaterial({
			uniforms: {
				tex2d: { value: _threeObjects.texture },
			},
			vertexShader: _hasBasisTexture ? vertBasis : vert,
			fragmentShader: frag,
		});
		_threeObjects.mesh = new THREE.Mesh(_threeObjects.geometry, _threeObjects.material);
		_threeObjects.mesh.rotation.y = Math.PI;
		_threeObjects.mesh.frustumCulled = false;
		_threeObjects.mesh.needsUpdate = true;
		_threeObjects.material.needsUpdate = true;
	};

	const _createVologram = () => {
		// It is important to create the geometry and texture before the mesh
		_createVologramGeometry();
		_createVologramTexture();
		_createVologramMesh();
	};

	const _updateTexture = () => {
		if (!_vologram.run_basis_transcode(_basisFmt)) return;
		const texData = _vologram.basis_get_data();
		//this.threeObjects.texture.mipmaps[0].data.set(texData);
		_threeObjects.texture.dispose();
		_threeObjects.texture = new THREE.CompressedTexture(
			[{ data: texData, width: _vologram.header.textureWidth, height: _vologram.header.textureHeight }],
			_vologram.header.textureWidth,
			_vologram.header.textureHeight,
			_glFmt,
			THREE.UnsignedByteType
		);
		_threeObjects.texture.minFilter = THREE.LinearFilter;
		_threeObjects.texture.needsUpdate = true;
		_threeObjects.material.uniforms.tex2d.value = _vologram.three.texture;
		_threeObjects.material.needsUpdate = true;
	};

	const _updateMesh = () => {
		if (!_threeObjects.geometry) {
			return false;
		}

		// Positions - fetch and upload.
		_threeObjects.geometry.setAttribute("position", new THREE.Float32BufferAttribute(_vologram.frame.positions, 3));

		if (_vologram.three.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			_threeObjects.geometry.setAttribute("normal", new THREE.Float32BufferAttribute(_vologram.frame.normals, 3));
		}

		// Key-Frames also contain texture coordinate and index data.
		if (_vologram.frame.isKey) {
			// Texture Coordinates - fetch and upload.
			_threeObjects.geometry.setAttribute("uv", new THREE.Float32BufferAttribute(_vologram.frame.texCoords, 2));

			// Indices - fetch and upload.
			_threeObjects.geometry.setIndex(new THREE.Uint16BufferAttribute(_vologram.frame.indices, 1));
		}

		// It seems that calculating bounding sphere does not work
		// and always returns NaN value for the radius
		if (_threeObjects.geometry.boundingSphere === null) {
			_threeObjects.geometry.boundingSphere = new THREE.Sphere();
		}
		if (_threeObjects.geometry.boundingSphere.radius <= 0 || Number.isNaN(_threeObjects.geometry.boundingSphere.radius)) {
			_threeObjects.geometry.computeBoundingBox();
			_threeObjects.geometry.boundingBox.getBoundingSphere(_threeObjects.geometry.boundingSphere);
		}
		_threeObjects.geometry.needsUpdate = true;
		_threeObjects.mesh.needsUpdate = true;
		return true;
	};

	const _update = (vologram) => {
		_updateMesh();
		if (_hasBasisTexture) _updateTexture();
	};

	const _close = () => {
		_basePlayer.removeEventListener("onframeready", _update);
		_basePlayer.removeEventListener("onclose", _close);
		if (_vologram && _threeObjects) {
			_threeObjects.mesh = null;
			_threeObjects.material = null;
			_threeObjects.geometry = null;
			_threeObjects.texture.dispose();
			_threeObjects.texture = null;
		}
	};

	_basePlayer = basePlayer;
	_vologram = basePlayer.getVologram();
	_vologram.three = {};
	_threeObjects = _vologram.three;
	if (_vologram.header.textureCompression == 1 || _vologram.header.textureCompression == 2) {
		_hasBasisTexture = true;
		const fmts = _vologram.find_basis_fmt(renderer3js.getContext());
		_glFmt = fmts[0];
		_basisFmt = fmts[1];
	}
	_createVologram();
	_basePlayer.addEventListener("onframeready", _update);
	_basePlayer.addEventListener("onclose", _close);

	const getMesh = () => _vologram.three.mesh;

	return {
		getMesh,
	};
};
