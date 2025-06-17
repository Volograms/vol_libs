# vol_libs/wasm

JavaScript modules useful for volograms playback on web, as well as the resources needed to build the modules

The base module is designed to grab the volograms data and make it accessible to the user. This way, a developer can use the player in whatever environment. There are also player scripts that can be used in WebGL or Three JS projects.

Please refer to the following examples for support:

- [Base Module](../examples/04_vol_geom_wasm_webgl/)
- [Players (CDN)](../examples/05_vol_player_wasm/)
- [Players (NPM)](../examples/07_vol_player_wasm/)
  - Requires [`npm`](https://docs.npmjs.com/downloading-and-installing-node-js-and-npm), [`npx`](https://www.npmjs.com/package/npx)

## Importing Modules in your Projects

You can import the modules into your projects a number of ways:

### NPM

- Open your terminal and `cd` into your node project
- Run the command `npm i @volograms/web_vol_lib`
- Import the module into your script using:

```js
// Required
import VologramPlayer from "@volograms/web_vol_lib/players/VologramPlayer.mjs";
// Optional (Recommended for WebGL projects)
import WebGlPlayerExtension from "@volograms/web_vol_lib/players/WebGlPlayerExtension.mjs";
// Optional (Recommended for Three JS projects)
import ThreeJsPlayerExtension from "@volograms/web_vol_lib/players/ThreeJsPlayerExtension.mjs";
// Optional (Recommonded only if you need to directly interface with the wasm module)
import VolWeb from "@volograms/web_vol_lib/vol_web.mjs";
```

### CDN

cdn is hosted by [jsdelivr](https://www.jsdelivr.com)

- In your HTML file you can use a `script` to import the players:

```html
<!-- Required -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/players/VologramPlayer.js"></script>
<!-- Optional (Recommended for WebGL projects) -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/players/WebGlPlayerExtension.js"></script>
<!-- Optional (Recommended for Three JS projects) -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/players/ThreeJsPlayerExtension.js"></script>
<!-- Optional (Recommonded only if you need to directly interface with the wasm module) -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/vol_web.js"></script>
```

- Now in a javascript you can access the players with:

```js
const vologramPlayer = VologramPlayer();
const vologramWebGl = VologramPlayer([WebGlPlayerExtension(gl)]);
const vologramThree = VologramPlayer([ThreeJsPlayerExtension(gl)]);
```

## `VologramPlayer`

`VologramPlayer(extensions)`: returns an object that provides properties functions to control the vologram playback

- Parameter `extensions`: (_**optional**_) an array of player extensions, for example the [`WebGlPlayerExtension`](#webglplayerextension) or [`ThreeJsPlayerExtension`](#threejsplayerextension)

The returned object has the following properties and functions:

### Properties

`.vologram`: returns the vologram object (see [reference](#vologram-object))

`.mute`: returns `true` if the vologram is playing audio and `false` otherwise, can also set it to enable or disable audio

`.loop`: returns `true` if the vologram playback loops to the start when finished and `false` otherwise, can also set it to enable or disable looping

`.extensions`: returns the exported properties and functions of the player's extensions (e.g. the [`WebGlPlayerExtension`](#webglplayerextension) or [`ThreeJsPlayerExtension`](#threejsplayerextension))

### Functions

`.start()`: begins playback from the **start** of the vologram

`.pause()`: pauses the vologram

`.play()`: resumes playback from the **current** playback time

`.isPlaying()`: returns `true` if playback is active, and `false` otherwise

`.open(input, onProgressCallback)`: open a new vologram

- Parameter `input`: (**_required_**) object with the following fields:
  - `headerUrl`: (**_optional_**) url or string to the vologram header file, if null player assumes all the vologram data is contained in the sequence file
  - `sequenceUrl`: (**_required_**) url or string to the vologram sequence file
  - `textureUrl`: (**_optional_**) url or string to the texture video file
  - `videoElement`: (**_optional_**) HTML video element that will contain the video texture, if null the player will create its own if needed
  - `audioElement`: (**_optional_**) HTML audio element that will contain the vologram audio, if null the player will create its own if needed
  - `useWorker`: (**_optional_**) If Basis Universal texture should be decoded in a web worker or not, `false` on default.
- Parameter `onProgressCallback`: (**_optional_**) a callback function that accepts a float value between 0 and 1 representing the download progress of the vologram file(s)

`.close()`: closes an open vologram

`.registerCallback(event, callback)`: set a function to be called when an event occurs during playback

- Parameter `event`: (_**required**_) must on of the following:
  - `"onclose"`: triggered when `close()` is called
  - `"onended"`: triggered when vologram reached the end of the playback
  - `onbuffering`: triggered when vologram reached the end of available stream and is buffering. `callback` has one input (`true|false`) depending on buffering state.
- Parameter `callback`: (_**required**_) function that is called when the event is triggered, has no inputs

`.unregisterCallback(event, callback)`: remove a function that was registered to an event with `registerCallback`

- Parameter `event`: (_**required**_) must be the same as the event parameter of the `registerCallback` call
- Parameter `callback`: (_**required**_) must be the same as the callback parameter of the `registerCallback` call

## `WebGlPlayerExtension`

`WebGlPlayerExtension(glContext)`: returns an extension object that can be passed into the [`VologramPlayer`](#vologramplayer) function

- Parameter `glContext`: (_**required**_) a [`WebGL2RenderingContext`](https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext) object

The extension provides the following properties and functions that can be accessed through `vologramPlayer.extensions.webgl`:

### Properties

`.objects`: returns an object with the following members:

- `.vertexArrayObject`: the vertex array object of the vologram mesh data
- `.positionsBuffer`: the buffer object of the mesh vertex positions
- `.uvsBuffer`: the buffer object of the mesh texture coordinates
- `.normalsBuffer`: the buffer object of the mesh normals (`undefined` if the vologram has no normals)
- `.indicesBuffer`: the buffer object of the mesh indices
- `.texture`: the texture buffer
- `.shaderProgram`: the shader program for rendering the vologram (`undefined` until set)
- `.positionShaderAttribute`: an object containing the `index` and `name` of the the position shader attribute (`undefined` until set)
- `.uvsShaderAttribute`: an object containing the `index` and `name` of the the texture coordinates shader attribute (`undefined` until set)
- `.normsShaderAttribute`: an object containing the `index` and `name` of the the normals shader attribute (`undefined` if the vologram has no normals, ot until set)
- `.textureTarget`: the shader texture target for the vologram texture (`undefined` until set)
- `.ready`: boolean indicating if the minimal required initialisation is complete

### Functions

`.bindPositionAttribute(index, name)`: sets the mesh vertex positions shader attribute

- Parameter `index`: (_**required**_) attribute location in the shader
- Parameter `name`: (_**required**_) attribute name in the shader

`.bindNormalsAttribute(index, name)`: sets the mesh normals shader attribute

- Parameter `index`: (_**required**_) attribute location in the shader
- Parameter `name`: (_**required**_) attribute name in the shader

`.bindUvsAttribute(index, name)`: sets the mesh texture coordinates shader attribute

- Parameter `index`: (_**required**_) attribute location in the shader
- Parameter `name`: (_**required**_) attribute name in the shader

`.setTextureTarget(glInt)`: sets the texture target of the shader

- Parameter `glInt`: (_**required**_) [WebGL constant](https://developer.mozilla.org/en-US/docs/Web/API/WebGL_API/Constants) representing the texture target

`.setShaderProgram(program)`: sets the vologram shader program

- Paramater `program`: (_**required**_) [`WebGLProgram`](https://developer.mozilla.org/en-US/docs/Web/API/WebGLProgram) from calling [`createProgram()`](https://developer.mozilla.org/en-US/docs/Web/API/WebGLRenderingContext/createProgram)

`.renderUpdate()`: updates the meshdata and renders the vologram, shader, texture target and shader attributes must be set before calling this function

`.useDefaults()`: sets default shader-related values

`.beginRendering()`: the extension starts its own rendering loop

## `ThreeJsPlayerExtension`

`ThreeJsPlayerExtension(glContext)`: returns an extension object that can be passed into the [`VologramPlayer`](#vologramplayer) function

- Parameter `glContext`: (_**required**_) a [`WebGL2RenderingContext`](https://developer.mozilla.org/en-US/docs/Web/API/WebGL2RenderingContext) object

The extension provides the following properties and functions that can be accessed through `vologramPlayer.extensions.threejs`:

### Properties

`.objects`: returns an object with the following members:

- `.scene`: [Three.js Scene object](https://threejs.org/docs/#api/en/scenes/Scene), can be `undefined` if you are using your own scene
- `.camera`: [Three.js Camera object](https://threejs.org/docs/index.html?q=camera#api/en/cameras/Camera), can be `undefined` if you are using your own scene
- `.renderer`: [Three.js WebGLRenderer object](https://threejs.org/docs/index.html?q=renderer#api/en/renderers/WebGLRenderer), can be `undefined` if you are using your own scene
- `.mesh`: [Three.js Mesh object](https://threejs.org/docs/index.html?q=mesh#api/en/objects/Mesh) containing the vologram mesh
- `.geometry`: [Three.js BufferGeometry object](https://threejs.org/docs/index.html?q=geometry#api/en/core/BufferGeometry) containing the vologram mesh data
- `.material`: [Three.js Material object](https://threejs.org/docs/index.html?q=material#api/en/materials/Material) containing the material for rendering the vologram
- `.texture`: [Three.js Texture object](https://threejs.org/docs/index.html?q=Texture#api/en/textures/Texture) containing the vologram texture data

### Functions

- `.renderUpdate()`: updates the vologram mesh data for render

- `.createThreeJsScene(canvas)`: creates a scene, camera and renderer object with default settings

  - Parameter `canvas`: (_**required**_) HTML canvas element

- `.beginRendering()`: the extension starts its own rendering loop

## Vologram Object

- `.header`: object that contains the following members:
  - `.singleFile`: boolean value indicating if the vologram was loaded from a single file
  - `.hasNormal`: boolean value indicating if the vologram has normals
  - `.hasTexture`: boolean value indicating if the vologram has texture data included in the sequence file (instead of in a separate video file)
  - `.hasAudio`: boolean value indicating if the vologram has audio data
  - `.textureCompression`: 0 if vologram uses video file, 1 is ETC1S, 2 is UASTC
  - `.textureContainerFormat`: 0 is raw, 1 is basis, 2 is ktx2
  - `.textureWidth`: width of vologram texture in pixels
  - `.textureHeight`: height of vologram texture in pixels
  - `.hasBasisTexture`: set to `true` if `.textureCompression` is 1 and `.textureCompression` is 2, `undefined` otherwise
  - `.frameCount`: number of frames in the vologram
  - `.fps`: playback frame rate of the vologram
  - `.durationS`: duration of the vologram in seconds
  - `.ready`: boolean value indicating if the vologram has been initialised
- `.frame`: object that is populated when a vologram mesh is loaded, contains the following members:
  - `.isKey`: boolean value indicating if the current loaded frame is a key frame
  - `.positions`: buffer array containing the vertex position data
  - `.normals`: buffer array containing the mesh normal data, `undefined` if the vologram does not have normals
  - `.texCoords`: buffer array containing the texture coordinate data
  - `.numIndices`: the number of indices values the frame has
  - `.indices`: buffer array containing the indices data
- `.lastFrameLoaded`: vologram frame index of the most recently loaded frame
- `.lastKeyframeLoaded`: vologram frame index of the most recently loaded key frame
- `.lastUpdateTime`: timestamp (in seconds) of when the last frame update took place (resets to 0 on start/restart)
- `.attachedVideo`: attached HTML video element for texture playback (can be `undefined`)
- `.attachedAudio`: attached HTML audio element for vologram audio playback (can be `undefined`)
- `.headerUrl`: string or URL of the vologram's header file (can be `undefined`)
- `.sequenceUrl`: string or URL of the vologram's sequence file
- `.textureUrl`: string or URL of the vologram's video texture file (can be `undefined`)

### Functions

The vologram object has access to all the [functions listed here](#after-initvologramfunctions), however the [VologramPlayer object](#vologramplayer) provides a more friendly interface and handles the data loading for you.

However, should your project needed them, feel free to make use of these more advanced functions.

## Using the Module

To initialise the vologram wasm module you need to call the
Module initialise function. But before, its important to set
the `onRuntimeInitialised` property of the module object. This property is a callback function when the Module has initialised.
Here we create the vologram related functions using `Module.initVologramFunctions()`.

```js
const Module = {};
Module["onRuntimeInitialized"] = function () {
	Module.initVologramFunctions();
};
VolWeb(Module);
```

`Module.initVologramFunctions()` can only be called once the Module object has been initialized. It calls [emscripten's `cwrap` method](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#interacting-with-code-ccall-cwrap) on all the vologram wasm functions.

## Vologram WASM `Module` Reference

### Init

#### `initModule(Module: Object)`

vol_lib wasm's default import. Initialises the WASM module

Parameters:

- `Module: Object` - Object that gets populated during init

#### `Module.initVologramFunctions()`

**Only available after `WebVol` has finished.**

Initialise and expose vologram related wasm functions (for convenience, but recommended)

---

### After `initVologramFunctions`

**These functions are only available if `initVologramFunctions` was called**

#### `Module.audio_data_ptr(): Array`

Returns the pointer to the audio data buffer

#### `Module.audio_data_sz(): Number`

Returns the size in bytes of the audio data buffer

#### `Module.basis_get_data(): [Number]`

Returns the transcoded texture data as a byte array (calls `basis_get_transcoded_ptr`, `texture_width` and `texture_height`)

#### `Module.basis_get_transcoded_ptr(): Number`

Returns the pointer to the transcoded texture data.

#### `Module.basis_get_transcoded_sz(): Number`

Returns the size in bytes of the transcoded texture data.

#### `Module.basis_transcode(format: Number, dataPtr: Number, dataSize: Number): Boolean`

Perform basis transcode on texture data. Returns `true` is successful and `false` otherwise.

Parameters:

- `format: Number` - Basis format
- `dataPtr: Number` - Pointer to texture data buffer
- `dataSize: Number` - Size, in bytes, of texture data

#### `Module.create_file_info(hdr: String, seq: String): Boolean`

Opens and initialses a vologram from separate header and sequence files. Returns `true` if successful and `false` otherwise.

Parameters:

- `hdr: String` - Path of vologram header file
- `seq: String` - Path of vologram sequence file
-

#### `Module.create_single_file_info(file: String): Boolean`

Opens and initialses a vologram from a single file. Returns `true` if successful and `false` otherwise.

Parameters:

- `file: String` - Path of vologram file

#### `Module.find_basis_fmt(gl: RenderingContext, hasAlpha: Boolean = true): [Number, Number]`

Calculates the supported GL format and basis format of the rendering context and returns them as an array of 2 numbers like so: \[ GL, Basis \]

#### `Module.find_previous_keyframe(frame_idx: Number): Number`

Returns the index of the frame that contains the keyframe data of frame `frame_idx` (returns `frame_idx` if that frame is a keyframe).

Parameters:

- `frame_idx: Number` - Frame index to check

#### `Module.frame_count(): Number`

Returns the number of frames in the vologram

#### `Module.frame_data_ptr(): Array`

Returns pointer to loaded frame data

#### `Module.frame_get_ind(): [Number]`

Returns the indices data as a byte array (calls `frame_indices_copied` and `frame_i_sz`)

#### `Module.frame_get_norms(): [Number]`

Returns the normals data as a byte array (calls `frame_normals_copied` and `frame_normals_sz`)

#### `Module.frame_get_uvs(): [Number]`

Returns the texture coordinate data as a byte array (calls `frame_uvs_copied` and `frame_uvs_sz`)

#### `Module.frame_get_verts(): [Number]`

Returns the vertices data as a byte array (calls `frame_vp_copied` and `frame_vertices_sz`)

#### `Module.frame_indices_copied(): Array`

Returns a byte array of the copied indices data.

#### `Module.frame_indices(): Array`

Returns a byte array of the loaded indices data.

#### `Module.frame_i_sz(): Number`

Returns the size (in bytes) of the indices data.

#### `Module.frame_normals_copied(): Array`

Returns an array of copied vertex normal data.

#### `Module.frame_normals_sz(): Number`

Returns the size (in bytes) of the loaded normals data.

#### `Module.frame_uvs_copied(): Array`

Returns an array of copied texture coordinate data.

#### `Module.frame_uvs_sz(): Number`

Returns the size (in bytes) of the loaded texture coordinate data.

#### `Module.frame_vertices(): Array`

Returns a byte array of the loaded vertices position data.

#### `Module.frame_vertices_sz(): Number`

Returns the size (in bytes) of the loaded vertices position data.

#### `Module.frame_vp_copied(): Array`

Returns an array of copied vertex position data.

#### `Module.frame_vp_offset(): Number`

Returns the frame vertices data offset

#### `Module.free_file_info(): Boolean`

Dispose of vologram info. The vologram cannot be played after this function is called. Returns `true` if successful and `false` otherwise.

#### `Module.get_audio_data(): [Number]`

Returns the audio data as a byte array, calls `audio_data_ptr` and `audio_data_sz`.

#### `Module.has_normals(): Boolean`

Returns `true` if the loaded vologram has normals, `false` otherwise.

#### `Module.has_texture(): Boolean`

Returns `true` if the loaded vologram has an internal texture, `false` otherwise.

#### `Module.is_keyframe(frame_idx: Number): Boolean`

Returns `true` if the vologram frame numbered `frame_idx` is a key frame, or `false` otherwise.

Parameters:

- `frame_idx: Number` - Frame index to check

#### `Module.loaded_frame_number(): Number`

Returns the index number of the current loaded frame.

#### `Module.max_blob_sz(): Number`

Returns the size (in bytes) of the largest vologram frame.

#### `Module.read_frame(frame_idx: Number): Boolean`

Reads a frame of the vologram. Returns `true` if successful and `false` otherwise.

Parameters:

- `frame_idx: Number` - Frame index to read

#### `Module.run_basis_transcode(size: Number): Boolean`

Performs the same function as `basis_transcode` but the pointer and size of the data is calculated based on vologram frame data

Parameters:

- `format: Number` - Basis format

#### `Module.texture_compression(): Number`

Returns the compression type of the vologram texture

#### `Module.texture_container_format(): Number`

Returns the container type of the vologram texture

#### `Module.texture_height(): Number`

Returns the height of the vologram texture

#### `Module.texture_width(): Number`

Returns the width of the vologram texture

---

### Without `initVologramFunctions`

#### Unavailable Functions

The following functions **cannot** be called unless `initVologramFunctions` has been called beforehand:

- `basis_get_data()`
- `find_basis_fmt()`
- `frame_get_ind()`
- `frame_get_norms()`
- `frame_get_uvs()`
- `frame_get_verts()`
- `get_audio_data()`

#### Calling Available Functions

If you have not called `initVologramFunctions` you can still call the volograms wasm function using emscripten's `ccall`. To do this you need to pass in the name of the c function you want to call, the return type as a string, a string array of the parameter types and an array containing the parameters.

For example:

```js
Module.create_file_info("hdr.vols", "seq.vols");

// becomes...

Module.ccall("create_file_info", "boolean", ["string", "string"], ["hdr.vols", "seq.vols"]);
```

The following table shows the correct parameters for ccall-ing the vologram functions

| Func                       | C name                       | Ret type    | Param Types                      |
| :------------------------- | :--------------------------- | :---------- | :------------------------------- |
| `audio_data_ptr`           | `"audio_data_ptr"`           | `"number"`  |                                  |
| `audio_data_sz`            | `"audio_data_sz"`            | `"number"`  |                                  |
| `basis_get_transcoded_ptr` | `"basis_get_transcoded_ptr"` | `"number"`  |                                  |
| `basis_get_transcoded_sz`  | `"basis_get_transcoded_sz"`  | `"number"`  |                                  |
| `basis_transcode`          | `"basis_transcode"`          | `"boolean"` | `["number", "number", "number"]` |
| `create_file_info`         | `"create_file_info"`         | `"boolean"` | `["string", "string"]`           |
| `create_single_file_info`  | `"create_single_file_info"`  | `"boolean"` | `["string"]`                     |
| `find_previous_keyframe`   | `"find_previous_keyframe"`   | `"number"`  | `["number"]`                     |
| `frame_count`              | `"frame_count"`              | `"number"`  |                                  |
| `frame_data_ptr`           | `"frame_data_ptr"`           | `"array"`   |                                  |
| `frame_indices_copied`     | `"frame_indices_copied"`     | `"array"`   |                                  |
| `frame_indices`            | `"frame_i"`                  | `"array"`   |                                  |
| `frame_i_sz`               | `"frame_i_sz"`               | `"number"`  |                                  |
| `frame_normals_copied`     | `"frame_normals_copied"`     | `"array"`   |                                  |
| `frame_normals_sz`         | `"frame_normals_sz"`         | `"number"`  |                                  |
| `frame_uvs_copied`         | `"frame_uvs_copied"`         | `"array"`   |                                  |
| `frame_uvs_sz`             | `"frame_uvs_sz"`             | `"number"`  |                                  |
| `frame_vertices`           | `"frame_vertices"`           | `"array"`   |                                  |
| `frame_vertices_sz`        | `"frame_vertices_sz"`        | `"number"`  |                                  |
| `frame_vp_copied`          | `"frame_vp_copied"`          | `"array"`   |                                  |
| `frame_vp_offset`          | `"frame_vp_offset"`          | `"number"`  |                                  |
| `free_file_info`           | `"free_file_info"`           | `"boolean"` |                                  |
| `has_normals`              | `"has_normals"`              | `"boolean"` |                                  |
| `has_texture`              | `"has_texture"`              | `"boolean"` |                                  |
| `is_keyframe`              | `"is_keyframe"`              | `"boolean"` | `["number"]`                     |
| `loaded_frame_number`      | `"loaded_frame_number"`      | `"number"`  |                                  |
| `max_blob_sz`              | `"max_blob_sz"`              | `"number"`  |                                  |
| `read_frame`               | `"read_frame"`               | `"boolean"` | `["number"]`                     |
| `run_basis_transcode`      | `"run_basis_transcode"`      | `"boolean"` | `["number"]`                     |
| `texture_compression`      | `"texture_compression"`      | `"number"`  |                                  |
| `texture_container_format` | `"texture_container_format"` | `"number"`  |                                  |
| `texture_width`            | `"texture_width"`            | `"number"`  |                                  |
| `texture_height`           | `"texture_height"`           | `"number"`  |                                  |

---

## Building Your Own Modules

### Requirements

- [emscripten](https://emscripten.org/docs/getting_started/index.html) is used to build the js modules from c code

### Build

Run the `wasm/build.sh` script to build a new `vol_geom.mjs` file

## Repository Contents

TODO
