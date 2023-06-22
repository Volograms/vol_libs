/** @typedef {import('../types/Vologram').Vologram} Vologram */
/** @typedef {import('../types/VologramPlayer').VologramPlayerExtensionConstructor} VologramPlayerExtensionConstructor */

const POS_ATTR_LOC = 0;
const NOR_ATTR_LOC = 1;
const UVS_ATTR_LOC = 2;
const POS_ATTR_NAME = "position";
const NOR_ATTR_NAME = "normal";
const UVS_ATTR_NAME = "uv";

const VERT = /*glsl*/ `
precision mediump float;
#define PI 3.1415926538

attribute vec3 ${POS_ATTR_NAME};
attribute vec2 ${UVS_ATTR_NAME};
attribute vec3 ${NOR_ATTR_NAME};

varying vec2 v_st;

mat4 rotationY( in float angle ) {
	return mat4(
		cos(angle), 	0, 		sin(angle),	0,
		0,						1.0,	0,					0,
		-sin(angle),	0,		cos(angle),	0,
		0, 						0,		0,					1
	);
}

void main () {
	vec4 p_loc = vec4( ${POS_ATTR_NAME}, 1.0 ) * rotationY(PI);
	p_loc.x *= -1.0;
	p_loc.y -= 1.0;
	p_loc *= 0.7;
	gl_Position = p_loc;

	v_st = ${UVS_ATTR_NAME};
}
`;

const BASIS_VERT = /*glsl*/ `
precision mediump float;
#define PI 3.1415926538

attribute vec3 ${POS_ATTR_NAME};
attribute vec2 ${UVS_ATTR_NAME};
attribute vec3 ${NOR_ATTR_NAME};

varying vec2 v_st;

mat4 rotationY( in float angle ) {
	return mat4(
		cos(angle), 	0, 		sin(angle),	0,
		0,						1.0,	0,					0,
		-sin(angle),	0,		cos(angle),	0,
		0, 						0,		0,					1
	);
}

void main () {
	vec4 p_loc = vec4( ${POS_ATTR_NAME}, 1.0 ) * rotationY(PI);
	p_loc.x *= -1.0;
	p_loc.y -= 1.0;
	p_loc *= 0.7;
	gl_Position = p_loc;

	v_st = vec2(${UVS_ATTR_NAME}.x, 1.0 - ${UVS_ATTR_NAME}.y);
}
`;

const FRAG = /*glsl*/ `
precision mediump float;

uniform sampler2D map;
varying vec2 v_st;

void main () {
  vec3 texel_rgb = texture2D( map, v_st ).rgb;
  gl_FragColor.rgba = vec4( texel_rgb, 1.0 );
}`;

/** @type {VologramPlayerExtensionConstructor} */
const WebGlPlayerExtension = (glCtx, options) => {
	if (!glCtx) {
		console.error("Volograms WebGlPlayerExtension requires a gl rendering context");
		return;
	}
	/** @type {HTMLCanvasElement} */ const canvas = glCtx.canvas;

	const gl = glCtx;
	const objs = {};

	let _glFmt;
	let _basisFmt;
	let _callbackId;
	/** @type {Vologram} */ let vologram;

	const _init = (inVologram) => {
		vologram = inVologram;
		const fmts = vologram.find_basis_fmt(gl);
		_glFmt = fmts[0];
		_basisFmt = fmts[1];

		objs.vertexArrayObject = gl.createVertexArray();
		objs.positionsBuffer = gl.createBuffer(); // Vertex positions buffer object.
		objs.uvsBuffer = gl.createBuffer(); // Texture coordinates buffer object.
		if (vologram.header.hasNormals) objs.normalsBuffer = gl.createBuffer(); // Normals buffer object. TODO(Anton) check if normals are in vologram
		objs.indicesBuffer = gl.createBuffer(); // Index buffer object.
		objs.texture = gl.createTexture();
	};

	const bindToShader = () => {
		gl.bindAttribLocation(objs.shaderProgram, objs.positionShaderAttribute.index, objs.positionShaderAttribute.name); // Vertex position
		gl.bindAttribLocation(objs.shaderProgram, objs.uvsShaderAttribute.index, objs.uvsShaderAttribute.name); // Texture coordinate
		gl.bindAttribLocation(objs.shaderProgram, objs.normsShaderAttribute.index, objs.normsShaderAttribute.name); // Normal
	};

	const _useDefaultShader = () => {
		objs.positionShaderAttribute = { index: POS_ATTR_LOC, name: POS_ATTR_NAME };
		objs.uvsShaderAttribute = { index: UVS_ATTR_LOC, name: UVS_ATTR_NAME };
		objs.normsShaderAttribute = { index: NOR_ATTR_LOC, name: NOR_ATTR_NAME };
		objs.textureTarget = gl.TEXTURE0;

		const vertShader = gl.createShader(gl.VERTEX_SHADER);
		const fragShader = gl.createShader(gl.FRAGMENT_SHADER);

		gl.shaderSource(vertShader, vologram.header.hasBasisTexture ? BASIS_VERT : VERT);
		gl.shaderSource(fragShader, FRAG);
		gl.compileShader(vertShader);
		if (!gl.getShaderParameter(vertShader, gl.COMPILE_STATUS)) {
			console.error("ERROR compiling vert shader. log: " + gl.getShaderInfoLog(vertShader));
		}
		gl.compileShader(fragShader);
		if (!gl.getShaderParameter(fragShader, gl.COMPILE_STATUS)) {
			console.error("ERROR compiling frag shader. log: " + gl.getShaderInfoLog(fragShader));
		}

		objs.shaderProgram = gl.createProgram();

		gl.attachShader(objs.shaderProgram, vertShader);
		gl.attachShader(objs.shaderProgram, fragShader);

		bindToShader();

		gl.linkProgram(objs.shaderProgram);
		if (!gl.getProgramParameter(objs.shaderProgram, gl.LINK_STATUS)) {
			console.error("ERROR linking program. log: " + gl.getProgramInfoLog(objs.shaderProgram));
		}
		gl.validateProgram(objs.shaderProgram);
		if (!gl.getProgramParameter(objs.shaderProgram, gl.VALIDATE_STATUS)) {
			console.error("ERROR validating program. log: " + gl.getProgramInfoLog(objs.shaderProgram));
		}

		objs.shaderProgram.u_P_loc = gl.getUniformLocation(objs.shaderProgram, "u_P"); // Projection matrix uniform.
		objs.shaderProgram.u_V_loc = gl.getUniformLocation(objs.shaderProgram, "u_V"); // View matrix uniform.
		objs.shaderProgram.u_M_loc = gl.getUniformLocation(objs.shaderProgram, "u_M"); // Model matrix uniform.

		gl.deleteShader(vertShader); // Flag shader for deletion whenever shader program is deleted.
		gl.deleteShader(fragShader);
	};

	const _updateMesh = () => {
		return true;
	};

	const _updateVideoTexture = () => {
		gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
		gl.bindTexture(gl.TEXTURE_2D, objs.texture);
		gl.texImage2D(
			gl.TEXTURE_2D,
			0,
			gl.RGBA,
			vologram.attachedVideo.videoWidth,
			vologram.attachedVideo.videoHeight,
			0,
			gl.RGBA,
			gl.UNSIGNED_BYTE,
			vologram.attachedVideo
		);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		return true;
	};

	const _updateBasisTexture = () => {
		if (!vologram.run_basis_transcode(_basisFmt)) return false;
		const texData = vologram.basis_get_data();
		gl.bindTexture(gl.TEXTURE_2D, objs.texture);
		let level_index = 0;
		gl.compressedTexImage2D(
			gl.TEXTURE_2D,
			level_index,
			_glFmt,
			vologram.header.textureWidth,
			vologram.header.textureHeight,
			0,
			texData
		);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		return true;
	};

	const _update = () => {
		if (!_updateMesh()) return false;
		if (vologram.header.hasBasisTexture) return _updateBasisTexture();
		else if (vologram.attachedVideo) return _updateVideoTexture();
		return true;
	};

	const _renderUpdate = () => {
		if (!vologram.frame.indices || !_update()) {
			return false;
		}

		gl.useProgram(objs.shaderProgram);
		gl.bindVertexArray(objs.vertexArrayObject);

		// Positions - upload.
		gl.bindBuffer(gl.ARRAY_BUFFER, objs.positionsBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, vologram.frame.positions, gl.STATIC_DRAW);
		gl.vertexAttribPointer(objs.positionShaderAttribute.index, 3, gl.FLOAT, false, 0, 0);
		gl.enableVertexAttribArray(objs.positionShaderAttribute.index);

		if (vologram.header.hasNormals) {
			// Not all volograms include normals.
			// Normals - upload.
			gl.bindBuffer(gl.ARRAY_BUFFER, objs.normalsBuffer);
			gl.bufferData(gl.ARRAY_BUFFER, vologram.frame.normals, gl.STATIC_DRAW);
			gl.vertexAttribPointer(objs.normsShaderAttribute.index, 3, gl.FLOAT, false, 0, 0);
			gl.enableVertexAttribArray(objs.normsShaderAttribute.index);
		}

		// Texture Coordinates - upload.
		gl.bindBuffer(gl.ARRAY_BUFFER, objs.uvsBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, vologram.frame.texCoords, gl.STATIC_DRAW);
		gl.vertexAttribPointer(objs.uvsShaderAttribute.index, 2, gl.FLOAT, false, 0, 0);
		gl.enableVertexAttribArray(objs.uvsShaderAttribute.index);

		// Indices - upload.
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, objs.indicesBuffer);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, vologram.frame.indices, gl.STATIC_DRAW);
		return true;
	};

	const _animate = () => {
		if (_renderUpdate()) {
			gl.viewport(0, 0, canvas.clientWidth, canvas.clientHeight);
			gl.clearColor(147.0 / 255.0, 149.0 / 255.0, 237.0 / 255.0, 1.0);
			gl.cullFace(gl.BACK);
			//gl.frontFace(gl.CCW);
			//gl.frontFace(gl.CW);
			gl.enable(gl.CULL_FACE);
			gl.enable(gl.DEPTH_TEST);
			gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

			gl.useProgram(objs.shaderProgram);

			gl.activeTexture(objs.textureTarget);
			gl.bindTexture(gl.TEXTURE_2D, objs.texture);
			gl.bindVertexArray(objs.vertexArrayObject);
			gl.drawElements(gl.TRIANGLES, vologram.frame.indices.length, gl.UNSIGNED_SHORT, 0); // UNSIGNED_SHORT == uint16_t indices.
		}
		_callbackId = requestAnimationFrame(_animate);
	};

	const _close = () => {
		if (_callbackId) cancelAnimationFrame(_callbackId);
	};

	return {
		get name() {
			return "webgl";
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
			get useDefaults() {
				return _useDefaultShader;
			},
			get beginRendering() {
				return _animate;
			},
		},
	};
};
