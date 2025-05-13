const ThreeJsLoader = (options) => {
	// Process input options
	const three = THREE || (options?.threeModule ?? null);

	if (!three) {
		console.error("THREE module cannot be found");
		return null;
	}

	const _createNewVologramTexture = (loader, video) => {
		const { header } = loader;
		if (!header.hasBasisTexture && video) {
			return new three.VideoTexture(video);
		}

		const texDataSize = header.textureWidth * header.textureHeight;
		const data = new Uint8Array(texDataSize);
		const texture = new three.CompressedTexture(
			[{ data, width: header.textureWidth, height: header.textureHeight }],
			header.textureWidth,
			header.textureHeight,
			header.glFormat,
			three.UnsignedByteType
		);
		texture.minFilter = three.LinearFilter;
		texture.needsUpdate = true;
		return texture;
	};

	const _updateVologramTexture = (frame, texture) => {
		const { textureData } = frame;
		if (!textureData || !texture.isCompressedTexture) return false;
		texture.mipmaps[0].data.set(textureData);
		texture.minFilter = three.LinearFilter;
		texture.needsUpdate = true;
		return true;
	};

	const _loadFrameIntoGeometry = (frame, geometry) => {
		// Positions - fetch and upload.
		geometry.setAttribute("position", new three.Float32BufferAttribute(frame.positions, 3));

		if (frame.hasNormals) {
			// Not all volograms include normals.
			// Normals - fetch and upload.
			geometry.setAttribute("normal", new three.Float32BufferAttribute(frame.normals, 3));
		}

		// Texture Coordinates - fetch and upload.
		geometry.setAttribute("uv", new three.Float32BufferAttribute(frame.texCoords, 2));

		// Indices - fetch and upload.
		geometry.setIndex(new three.Uint16BufferAttribute(frame.indices, 1));
		geometry.needsUpdate = true;
		return true;
	};

	const _sampleVertexShader = (reverseUvs) => /*glsl*/ `
varying vec2 rev_uv;
void main () {
	rev_uv = vec2(uv.x, ${reverseUvs ? "1.0 - uv.y" : "uv.y"});
	gl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );
}
`;

	const _sampleFragmentShader = /*glsl*/ `
uniform sampler2D map;
varying vec2 rev_uv;
void main () {
	vec4 color = texture2D( map, rev_uv );
	gl_FragColor = color;
}
`;

	return {
		get createVologramTexture() {
			return _createNewVologramTexture;
		},
		get updateVologramTexture() {
			return _updateVologramTexture;
		},
		get loadFrameIntoGeometry() {
			return _loadFrameIntoGeometry;
		},
		get sampleVertexShader() {
			return _sampleVertexShader;
		},
		get sampleFragmentShader() {
			return _sampleFragmentShader;
		},
	};
};
