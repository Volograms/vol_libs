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
uniform sampler2D map;
void main () {
  vec3 texel_rgb = texture2D( map, v_st ).rgb;
  // discard any magenta fragments.
  if ( texel_rgb.r > 0.9 && texel_rgb.g < 0.17 && texel_rgb.b > 0.9 ) { discard; }
  gl_FragColor = vec4( texel_rgb, 1.0 );
}`;

class VologramThreeJsExtension {
	#vologram = {};
	#glFmt;
	#basisFmt;

	#hasBasisTexture = false;

	onUpdateFuncs = [];
	onLoopFuncs = [];

	#createVologramGeometry = () => {
		this.#vologram.three.geometry = new THREE.BufferGeometry();
	};

	#createVologramTexture = () => {
		if (this.#vologram.attachedVideo) {
			this.#vologram.three.texture = new THREE.VideoTexture(this.#vologram.attachedVideo);
			return;
		}

		const texDataSize = this.#vologram.textureWidth * this.#vologram.textureHeight;
		const data = new Uint8Array(texDataSize);
		this.#vologram.three.texture = new THREE.CompressedTexture(
			[{ data, width: this.#vologram.textureWidth, height: this.#vologram.textureHeight }],
			this.#vologram.textureWidth,
			this.#vologram.textureHeight,
			this.#glFmt,
			THREE.UnsignedByteType
		);
		this.#vologram.three.texture.needsUpdate = true;
	};

	#createVologramMesh = () => {
		this.#vologram.three.material = new THREE.ShaderMaterial({
			uniforms: {
				map: { value: this.#vologram.three.texture },
			},
			vertexShader: this.#hasBasisTexture ? vertBasis : vert,
			fragmentShader: frag,
		});
		this.#vologram.three.mesh = new THREE.Mesh(this.#vologram.three.geometry, this.#vologram.three.material);
		this.#vologram.three.mesh.frustumCulled = false;
		this.#vologram.three.mesh.needsUpdate = true;
		this.#vologram.three.material.needsUpdate = true;
	};

	#createVologram = () => {
		// It is important to create the geometry and texture before the mesh
		this.#createVologramGeometry();
		this.#createVologramTexture();
		this.#createVologramMesh();
	};

	#updateTexture = () => {
		if (!this.#vologram.run_basis_transcode(this.#basisFmt)) return;
		const texData = this.#vologram.basis_get_data();
		this.#vologram.three.texture.dispose();
		this.#vologram.three.texture = new THREE.CompressedTexture(
			[
				{
					data: texData,
					width: this.#vologram.textureWidth,
					height: this.#vologram.textureHeight,
				},
			],
			this.#vologram.textureWidth,
			this.#vologram.textureHeight,
			this.#glFmt,
			THREE.UnsignedByteType
		);
		this.#vologram.three.texture.needsUpdate = true;
		this.#vologram.three.material.uniforms.map.value = this.#vologram.three.texture;
		this.#vologram.three.material.needsUpdate = true;
	};

	#updateMesh = () => {
		if (!this.#vologram.three.geometry) {
			return false;
		}

		// Positions - fetch and upload.
		this.#vologram.three.geometry.setAttribute(
			"position",
			new THREE.Float32BufferAttribute(this.#vologram.frame.positions, 3)
		);

		if (this.#vologram.three.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			this.#vologram.three.geometry.setAttribute(
				"normal",
				new THREE.Float32BufferAttribute(this.#vologram.frame.normals, 3)
			);
		}

		// Key-Frames also contain texture coordinate and index data.
		if (this.#vologram.frame.isKey) {
			// Texture Coordinates - fetch and upload.
			this.#vologram.three.geometry.setAttribute(
				"uv",
				new THREE.Float32BufferAttribute(this.#vologram.frame.texCoords, 2)
			);

			// Indices - fetch and upload.
			this.#vologram.three.geometry.setIndex(new THREE.Uint16BufferAttribute(this.#vologram.frame.indices, 1));
		}

		// It seems that calculating bounding sphere does not work
		// and always returns NaN value for the radius
		if (this.#vologram.three.geometry.boundingSphere === null) {
			this.#vologram.three.geometry.boundingSphere = new THREE.Sphere();
		}
		if (
			this.#vologram.three.geometry.boundingSphere.radius <= 0 ||
			Number.isNaN(this.#vologram.three.geometry.boundingSphere.radius)
		) {
			this.#vologram.three.geometry.computeBoundingBox();
			this.#vologram.three.geometry.boundingBox.getBoundingSphere(this.#vologram.three.geometry.boundingSphere);
		}
		return true;
	};

	#update = (vologram) => {
		this.#updateMesh();
		if (this.#hasBasisTexture) this.#updateTexture();
	};

	#close = () => {
		if (this.#vologram && this.#vologram.three) {
			this.#vologram.three.mesh = null;
			this.#vologram.three.material = null;
			this.#vologram.three.geometry = null;
			this.#vologram.three.texture = null;
		}
	};

	constructor(renderer3js, basePlayer) {
		this.#vologram = basePlayer.vologram;
		this.#vologram.three = {};
		if (this.#vologram.header.textureCompression == 1 || this.#vologram.header.textureCompression == 2) {
			this.#hasBasisTexture = true;
			const fmts = this.vologram.find_basis_fmt(renderer3js.getContext());
			this.#glFmt = fmts[0];
			this.#basisFmt = fmts[1];
		}
		this.#createVologram();
		basePlayer.addEventListener("onframeready", this.#update);
		basePlayer.addEventListener("onclose", this.#close);
	}
}
