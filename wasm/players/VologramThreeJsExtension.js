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

class VologramThreeJsExtension {
	#vologram;
	#glFmt;
	#basisFmt;
	#basePlayer;
	threeObjects;

	#hasBasisTexture = false;

	#createVologramGeometry = () => {
		this.threeObjects.geometry = new THREE.BufferGeometry();
	};

	#createVologramTexture = () => {
		if (this.#vologram.attachedVideo) {
			this.threeObjects.texture = new THREE.VideoTexture(this.#vologram.attachedVideo);
			return;
		}

		const texDataSize = this.#vologram.header.textureWidth * this.#vologram.header.textureHeight;
		const data = new Uint8Array(texDataSize);
		this.threeObjects.texture = new THREE.CompressedTexture(
			[{ data: data, width: this.#vologram.header.textureWidth, height: this.#vologram.header.textureHeight }],
			this.#vologram.header.textureWidth,
			this.#vologram.header.textureHeight,
			this.#glFmt,
			THREE.UnsignedByteType
		);
		this.threeObjects.texture.minFilter = THREE.LinearFilter;
		this.threeObjects.texture.needsUpdate = true;
	};

	#createVologramMesh = () => {
		this.threeObjects.material = new THREE.ShaderMaterial({
			uniforms: {
				tex2d: { value: this.threeObjects.texture },
			},
			vertexShader: this.#hasBasisTexture ? vertBasis : vert,
			fragmentShader: frag,
		});
		this.threeObjects.mesh = new THREE.Mesh(this.threeObjects.geometry, this.threeObjects.material);
		this.threeObjects.mesh.rotation.y = Math.PI;
		this.threeObjects.mesh.frustumCulled = false;
		this.threeObjects.mesh.needsUpdate = true;
		this.threeObjects.material.needsUpdate = true;
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
		this.threeObjects.texture.mipmaps[0].data.set(texData);
		this.threeObjects.texture.minFilter = THREE.LinearFilter;
		this.threeObjects.texture.needsUpdate = true;
		this.threeObjects.material.uniforms.tex2d.value = this.#vologram.three.texture;
		this.threeObjects.material.needsUpdate = true;
	};

	#updateMesh = () => {
		if (!this.threeObjects.geometry) {
			return false;
		}

		// Positions - fetch and upload.
		this.threeObjects.geometry.setAttribute(
			"position",
			new THREE.Float32BufferAttribute(this.#vologram.frame.positions, 3)
		);

		if (this.#vologram.three.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			this.threeObjects.geometry.setAttribute("normal", new THREE.Float32BufferAttribute(this.#vologram.frame.normals, 3));
		}

		// Key-Frames also contain texture coordinate and index data.
		if (this.#vologram.frame.isKey) {
			// Texture Coordinates - fetch and upload.
			this.threeObjects.geometry.setAttribute("uv", new THREE.Float32BufferAttribute(this.#vologram.frame.texCoords, 2));

			// Indices - fetch and upload.
			this.threeObjects.geometry.setIndex(new THREE.Uint16BufferAttribute(this.#vologram.frame.indices, 1));
		}

		// It seems that calculating bounding sphere does not work
		// and always returns NaN value for the radius
		if (this.threeObjects.geometry.boundingSphere === null) {
			this.threeObjects.geometry.boundingSphere = new THREE.Sphere();
		}
		if (
			this.threeObjects.geometry.boundingSphere.radius <= 0 ||
			Number.isNaN(this.threeObjects.geometry.boundingSphere.radius)
		) {
			this.threeObjects.geometry.computeBoundingBox();
			this.threeObjects.geometry.boundingBox.getBoundingSphere(this.threeObjects.geometry.boundingSphere);
		}
		this.threeObjects.geometry.needsUpdate = true;
		this.threeObjects.mesh.needsUpdate = true;
		return true;
	};

	#update = (vologram) => {
		this.#updateMesh();
		if (this.#hasBasisTexture) this.#updateTexture();
	};

	#close = () => {
		this.#basePlayer.removeEventListener("onframeready", this.#update);
		this.#basePlayer.removeEventListener("onclose", this.#close);
		if (this.#vologram && this.threeObjects) {
			this.threeObjects.mesh = null;
			this.threeObjects.material = null;
			this.threeObjects.geometry = null;
			this.threeObjects.texture.dispose();
			this.threeObjects.texture = null;
		}
	};

	constructor(renderer3js, basePlayer) {
		this.#basePlayer = basePlayer;
		this.#vologram = basePlayer.vologram;
		this.#vologram.three = {};
		this.threeObjects = this.#vologram.three;
		if (this.#vologram.header.textureCompression == 1 || this.#vologram.header.textureCompression == 2) {
			this.#hasBasisTexture = true;
			const fmts = this.#vologram.find_basis_fmt(renderer3js.getContext());
			this.#glFmt = fmts[0];
			this.#basisFmt = fmts[1];
		}
		this.#createVologram();
		this.#basePlayer.addEventListener("onframeready", this.#update);
		this.#basePlayer.addEventListener("onclose", this.#close);
	}
}
