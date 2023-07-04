# vol_libs/wasm

JavaScript modules useful for volograms playback on web, as well as the resources needed to build the modules

The base module is designed to grab the volograms data and make it accessible to the user. This way, a developer can use the player in whatever environment. There are also player scripts that can be used in WebGL or Three JS projects.

Please refer to the following examples for support:

- [Base Module](../examples/04_vol_geom_wasm_webgl/)
- [Players](../examples/05_vol_player_wasm/)

## Importing Modules in your Projects

You can import the modules into your projects a number of ways:

### NPM

- Open your terminal and `cd` into your node project
- Run the command `npm i @volograms/web_vol_lib`
- Import the module into your script using:

```js
import initModule from "@volograms/web_vol_lib/vol_web.mjs";
```

### CDN

cdn is hosted by [jsdelivr](https://www.jsdelivr.com)

- In your HTML file you can use a `script` to import the players:

```html
<!-- Required -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/vol_web.js"></script>
<!-- Required -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/players/VologramPlayer.js"></script>
<!-- Optional (Recommended for WebGL projects) -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/players/WebGlPlayerExtension.js"></script>
<!-- Optional (Recommended for Three JS projects) -->
<script src="https://cdn.jsdelivr.net/gh/Volograms/vol_libs@main/wasm/players/ThreeJsPlayerExtension.js"></script>
```

- Now in a javascript you can access the players with:

```js
const vologramPlayer = VologramPlayer();
const vologramWebGl = VologramPlayer([WebGlPlayerExtension(gl)]);
const vologramThree = VologramPlayer([ThreeJsPlayerExtension(gl)]);
```

## Using the Module

To initialise the vologram wasm module you need to call the
Module initialise function. But before, its important to set
the `onRuntimeInitialised` property of the module object. This property is a callback function when the Module has initialised.
Here we create the vologram related functions using `Module.initVologramFunctions()`.

```js
Module["onRuntimeInitialized"] = function () {
	Module.initVologramFunctions();
};
initModule(Module);
```

`Module.initVologramFunctions()` can only be called once the Module object has been initialized. It calls [emscripten's `cwrap` method](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#interacting-with-code-ccall-cwrap) on all the vologram wasm functions.

## Vologram WASM `Module` Reference

### Init

#### `initModule(Module: Object)`

vol_lib wasm's default import. Initialises the WASM module

Parameters:

- `Module: Object` - Object that gets populated during init

#### `Module.initVologramFunctions()`

**Only available after `initModule` has finished.**

Initialise and expose vologram related wasm functions (for convenience, but recommended)

---

### After `initVologramFunctions`

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
