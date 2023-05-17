const vert = /*glsl*/`
precision mediump float;
varying vec2 v_st;
void main () {
  gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  v_st = vec2(uv.x, 1.0 - uv.y);
}
`

const frag = /*glsl*/`
precision mediump float;
varying vec2 v_st;
uniform sampler2D map;
void main () {
  vec3 texel_rgb = texture2D( map, v_st ).rgb;
  // discard any magenta fragments.
  if ( texel_rgb.r > 0.9 && texel_rgb.g < 0.17 && texel_rgb.b > 0.9 ) { discard; }
  gl_FragColor = vec4( texel_rgb, 1.0 );
}`

export class VologramThreeJsPlayer {
  #audio
  #vologram = {}
  #vologramInterface = {}
  #initDone = false
  #glFmt
  #basisFmt

  #frameFromTime = 0
  #timer = 0
  #timeDelta = 0
  #timeLastFrame = 0
  #timeThres = 1.0 / 30.0

  onUpdateFuncs = []
  onLoopFuncs = []

  #createVologramGeometry = () => {
    this.#vologram.three.geometry = new THREE.BufferGeometry()
  }
  
  #createVologramTexture = () => {
    const texDataSize = this.#vologram.three.textureWidth * this.#vologram.three.textureHeight
    const data = new Uint8Array(texDataSize)
    this.#vologram.three.texture = new THREE.CompressedTexture(
      [{data, width: this.#vologram.three.textureWidth, height: this.#vologram.three.textureHeight}],
      this.#vologram.three.textureWidth,
      this.#vologram.three.textureHeight,
      this.#glFmt,
      THREE.UnsignedByteType
    )
    this.#vologram.three.texture.needsUpdate = true
  }
  
  #createVologramMesh = () => {
    this.#vologram.three.material = new THREE.ShaderMaterial({
      uniforms: {
        map: {value: this.#vologram.three.texture},
      },
      vertexShader: vert,
      fragmentShader: frag,
    })
    this.#vologram.three.mesh = new THREE.Mesh(this.#vologram.three.geometry, this.#vologram.three.material)
    this.#vologram.three.mesh.frustumCulled = false
    this.#vologram.three.mesh.needsUpdate = true
    this.#vologram.three.material.needsUpdate = true
  }
  
  #createAudio = () => {
    this.#audio = document.createElement('audio')
    this.#audio.id = 'vol3jsAudio'
    this.#audio.width = 0
    this.#audio.height = 0
    document.body.prepend(this.#audio)
    const audioData = this.#vologramInterface.get_audio_data()
    const blob = new Blob([audioData], {type: 'audio/mpeg'})
    const blobUrl = URL.createObjectURL(blob)
    window.addEventListener('beforeunload', (e) => {
      URL.revokeObjectURL(blobUrl)
    }, false)
    this.#audio.src = blobUrl
    this.#audio.addEventListener('ended', (event) => {
      this.onLoopFuncs.forEach((fnc) => {
        fnc()
      })
    })
  }
  
  #createVologram = () => {
    this.#vologramInterface.create_single_file_info('header.vols')
    this.#vologram.three.frameCount = this.#vologramInterface.frame_count()
    this.#vologram.three.textureWidth = this.#vologramInterface.texture_width()
    this.#vologram.three.textureHeight = this.#vologramInterface.texture_height()
    this.#vologram.three.hasNormals = this.#vologramInterface.has_normals()
    this.#vologram.three.hasAudio = this.#vologramInterface.has_audio()
    // It is important to create the geometry and texture before the mesh
    this.#createVologramGeometry()
    this.#createVologramTexture()
    this.#createVologramMesh()
    if (this.#vologram.three.hasAudio) {
      this.#createAudio()
    }
  }
  
  #textureUpdate = () => {
    if (!this.#vologramInterface.run_basis_transcode(this.#basisFmt)) return
    const texData = this.#vologramInterface.basis_get_data()
    this.#vologram.three.texture.dispose()
    this.#vologram.three.texture = new THREE.CompressedTexture(
      [
        {
          data: texData,
          width: this.#vologram.three.textureWidth,
          height: this.#vologram.three.textureHeight,
        },
      ],
      this.#vologram.three.textureWidth,
      this.#vologram.three.textureHeight,
      this.#glFmt,
      THREE.UnsignedByteType
    )
    this.#vologram.three.texture.needsUpdate = true
    this.#vologram.three.material.uniforms.map.value = this.#vologram.three.texture
    this.#vologram.three.material.needsUpdate = true
  }
  
  // Copy vol_geom frame `frame_idx` into vologram webgl mesh.
  // Returns true on success, and false on error loading frame.
  // This function assumes that any preceding, required, keyframe
  // has been resolved and loaded already.
  #meshFromFrame = (frameIdx) => {
    if (!this.#vologram.three.geometry) {
      return false
    }
    if (this.#vologram.three.lastFrameLoaded === frameIdx) {
      return false
    }  // Safety catch to avoid reloading the same frame twice.
  
    // Ask the vol_geom WASM to read the frame data from the vologram file into `_frame_data`.
    const ret = this.#vologramInterface.read_frame(frameIdx)
    if (!ret) {
      return false
    }
  
    const isKey = this.#vologramInterface.is_keyframe(frameIdx)
  
    // Positions - fetch and upload.
    const vpF32 = this.#vologramInterface.frame_get_verts()
    this.#vologram.three.geometry.setAttribute('position', new THREE.Float32BufferAttribute(vpF32, 3))
  
    if (this.#vologram.three.hasNormals) {  // Not all volograms include normals.
      // Normals - fetch and upload.
      const vnF32 = this.#vologramInterface.frame_get_norms()
      this.#vologram.three.geometry.setAttribute('normal', new THREE.Float32BufferAttribute(vnF32, 3))
    }
  
    // Key-Frames also contain texture coordinate and index data.
    if (isKey) {
      this.#vologram.three.lastKeyframeLoaded = frameIdx
  
      // Texture Coordinates - fetch and upload.
      const uvsF32 = this.#vologramInterface.frame_get_uvs()
      this.#vologram.three.geometry.setAttribute('uv', new THREE.Float32BufferAttribute(uvsF32, 2))
  
      // Indices - fetch and upload.
      const indicesU16 = this.#vologramInterface.frame_get_ind()
      this.#vologram.three.geometry.setIndex(new THREE.Uint16BufferAttribute(indicesU16, 1))
    }
  
    // It seems that calculating bounding sphere does not work
    // and always returns NaN value for the radius
    if (this.#vologram.three.geometry.boundingSphere === null) {
      this.#vologram.three.geometry.boundingSphere = new THREE.Sphere()
    }
    if (
      this.#vologram.three.geometry.boundingSphere.radius <= 0 ||
          Number.isNaN(this.#vologram.three.geometry.boundingSphere.radius)
    ) {
      this.#vologram.three.geometry.computeBoundingBox()
      this.#vologram.three.geometry.boundingBox.getBoundingSphere(this.#vologram.three.geometry.boundingSphere)
    }
    this.#vologram.three.lastFrameLoaded = frameIdx
    return true
  }
  
  // Calls mesh_from_frame() but first loads a keyframe, if required.
  updateMeshFrameAllowingSkip = (desiredFrameIndex) => {
    // int find_previous_keyframe( int frame_idx );
    const keyframeRequired = this.#vologramInterface.find_previous_keyframe(desiredFrameIndex)
  
    // If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
    if (this.#vologram.three.lastKeyframeLoaded !== keyframeRequired) {
      meshFromFrame(keyframeRequired)
    }
    // Load actual current frame.
    meshFromFrame(desiredFrameIndex)
  }
  
  #calculateFrameAdvance = (time) => {
    this.#frameFromTime = Math.floor(time / this.#timeThres)
    if (this.#frameFromTime === this.#vologram.three.lastFrameLoaded) {
      return false
    }
    if (this.#frameFromTime >= this.#vologram.three.frameCount) {
      // frameFromTime = 0
      this.onLoopFuncs.forEach((fnc) => {
        fnc()
      })
      return false
    }
    return true
  }
  
  #animate = () => {
    let time = 0
    if (this.#audio) {
      time = this.#audio.currentTime
    } else {
      const perfTime = performance.now() / 1000
      this.#timeDelta = perfTime - this.#timeLastFrame
      this.#timer += this.#timeDelta
      this.#timeLastFrame = perfTime
      time = this.#timer
    }
    if (this.#vologram.three.playing && this.#calculateFrameAdvance(time)) {
      this.#updateMeshFrameAllowingSkip(this.#frameFromTime)
      this.#textureUpdate()
      this.#vologram.three.time = time
      this.onUpdateFuncs.forEach((fnc) => {
        fnc({frame: this.#frameFromTime, time})
      })
    }
    this.#vologram.three.material.needsUpdate = true
  }
  
  tick = () => {
    if (!this.#initDone || !this.#vologram.three.playing) {
      return
    }
    animate()
  }
  
  setMute = (value) => {
    if (this.#audio) this.#audio.muted = value
  }
  
  pause = () => {
    this.#vologram.three.playing = false
    if (this.#audio) this.#audio.pause()
  }
  
  resume = () => {
    this.#vologram.three.playing = true
    if (this.#audio) this.#audio.play()
    else this.#timeLastFrame = performance.now() / 1000
  }
  
  start = () => {
    this.#frameFromTime = -1
    this.#timer = 0
    this.#timeDelta = 0
    if (this.#audio) {
      this.#audio.currentTime = 0
      this.#audio.play()
    } else {
      this.#timeLastFrame = performance.now() / 1000
    }
    this.#vologram.three.playing = true
  }
  
  close = () => {
    if (this.#audio) {
      this.#audio.remove()
    }
    if (this.#vologram && this.#vologram.three) {
      this.#vologram.three.mesh = null
      this.#vologram.three.material = null
      this.#vologram.three.geometry = null
      this.#vologram.three.texture = null
    }
    this.#initDone = false
  }

  constructor(renderer3js, vologramObj, volInterface) {
    this.#vologram = vologramObj
    this.#vologram.three = {}
    this.#vologramInterface = volInterface
    const fmts = this.#vologramInterface.find_basis_fmt(renderer3js.getContext())
    this.#glFmt = fmts[0]
    this.#basisFmt = fmts[1]
    this.#createVologram()
    this.#initDone = true
  }
}