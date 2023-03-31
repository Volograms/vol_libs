# vol_libs/wasm #

Javascript modules useful for volograms playback on web, as well as the resources needed to build the modules

The base module is designed to grab the volograms data and make it accessible to the user. This way, a developer can use the player in whatever environment. There is also an interface script that can be used in threejs projects. 

Please refer to the following examples for support:

* [Base Module](../examples/04_vol_geom_wasm_webgl/)
* [threejs](../examples/04_vol_geom_wasm_webgl/)

## Importing Modules in your Projects

You can import the modules into your projects a number of way 

### npm

* Open your terminal and `cd` into your node project
* Run the command `npm i @volograms/web_vol_lib`
* Import the modulke into your script using

```js
import initModule from "@volograms/web_vol_lib/vol_web.mjs";
```

### cdn 

* cdn is hosted by [jsdelivr](https://www.jsdelivr.com)
* In your HTML file you can use an `importmap` to import the modules:

```html
<script type="importmap">
{
    "imports": {
        "WebVolLib": 
            "https://cdn.jsdelivr.net/gh/Volograms/vol_libs@distribution-set-up/wasm/vol_web.mjs"
    }
}
```
* Now in a javascript you can access the module with:
```js
import initModule from "WebVolLib";
```
* Note the matching `WebVolLib` name; this can be whatever you like but they must be consistent in your project

### Source 

* You can also copy can paste the modules into your own project and reference them as another of your source files:

```js
import initModule from "path/to/your/vol_web.mjs";
```

* You can also the non-module version like so: 

```js
import initModule from "path/to/your/vol_web.js";

/** 
 * Note that the initialise function is imported with 
 * the name `Module` by default. However the following 
 * code makes the object and function names consistent 
 * with the mjs version.
 */
const initModule = Module;
Module = new Object();
```

## Using the Module

To initialise the vologram wasm module you need to call the 
Module initialise function. But before, its important to set
the `onRuntimeInitialised` property of the module object. This property is a callback function when the Module has initialised.
Here we create the vologram related functions using `Module.initVologramFunctions()`.

```js
Module['onRuntimeInitialized'] = function() {
    Module.initVologramFunctions();
}
initModule(Module);
```

`Module.initVologramFunctions()` can only be called once the Module object has been initialized. It calls [emscripten's `cwrap` method](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#interacting-with-code-ccall-cwrap) on all the vologram wasm functions. 

## Vologram WASM `Module` Reference 

### Init

#### `initModule(Module: Object)`

vol_lib wasm's default import. Initialises the WASM module

Parameters:
* `Module: Object` - Object that gets populated during init

#### `Module.initVologramFunctions()`

**Only available after `initModule` has finished.**

Initialise and expose vologram related wasm functions (for convenience, but recommended)

### After `initVologramFunctions`

#### `Module.create_file_info(hdr: String, seq: String): Boolean`

Open and initialse a vologram. Returns `true` if successful and `false` otherwise.

Parameters:
* `hdr: String` - Path of vologram header file
* `seq: String` - Path of vologram sequence file

#### `Module.find_previous_keyframe(frame_idx: Number): Number`

Returns the index of the frame that contains the keyframe data of frame `frame_idx` (returns `frame_idx` if that frame is a keyframe).

Parameters:
* `frame_idx: Number` - Frame index to check 

#### `Module.frame_count(): Number`

Returns the number of frames in the vologram 

#### `Module.frame_data_ptr(): Array`

Returns pointer to loaded frame data 

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

#### `Module.has_normals(): Boolean`

Returns `true` if the loaded vologram has normals, `false` otherwise.

#### `Module.is_keyframe(frame_idx: Number): Boolean` 

Returns `true` if the vologram frame numbered `frame_idx` is a key frame, or `false` otherwise. 

Parameters:
* `frame_idx: Number` - Frame index to check 

#### `Module.loaded_frame_number(): Number`

Returns the index number of the current loaded frame.

#### `Module.max_blob_sz(): Number`

Returns the size (in bytes) of the largest vologram frame.

#### `Module.read_frame(frame_idx: Number): Boolean` 

Reads a frame of the vologram. Returns `true` if successful and `false` otherwise.

Parameters:
* `frame_idx: Number` - Frame index to read 

### Without `initVologramFunctions`

If you have not called `initVologramFunctions` you can still call the volograms wasm function using emscripten's `ccall`. To do this you need to pass in the name of the c function you want to call, the return type as a string, a string array of the parameter types and an array containing the parameters.

For example:
```js
Module.create_file_info( "hdr.vols", "seq.vols" );

// becomes... 

Module.ccall( 'create_file_info', 'boolean', ['string', 'string'], ['hdr.vols', 'seq.vols'] );
```

The following table shows the correct parameters for ccall-ing the vologram functions

| Func | C name | Ret type | Param Types |
| :-- | :-- | :-- | :-- |
| `create_file_info` | `"create_file_info"` | `"boolean"` | `["string", "string"]` | 
| `find_previous_keyframe` | `"find_previous_keyframe"` | `"number"` | `["number"]` |
| `frame_count` | `"frame_count"` | `"number"` |  |
| `frame_data_ptr` | `"frame_data_ptr"` | `"array"` |  |
| `frame_indices_copied` | `"frame_indices_copied"` | `"array"` |  |
| `frame_indices` | `"frame_indices"` | `"array"` |  |
| `frame_i_sz` | `"frame_i_sz"` | `"number"` |  |
| `frame_normals_copied` | `"frame_normals_copied"` | `"array"` |  |
| `frame_normals_sz` | `"frame_normals_sz"` | `"number"` |  |
| `frame_uvs_copied` | `"frame_uvs_copied"` | `"array"` |  |
| `frame_uvs_sz` | `"frame_uvs_sz"` | `"number"` |  |
| `frame_vertices` | `"frame_vertices"` | `"array"` |  |
| `frame_vertices_sz` | `"frame_vertices_sz"` | `"number"` |  |
| `frame_vp_copied` | `"frame_vp_copied"` | `"array"` |  |
| `frame_vp_offset` | `"frame_vp_offset"` | `"number"` |  |
| `free_file_info` | `"free_file_info"` | `"boolean"` |  |
| `has_normals` | `"has_normals"` | `"boolean"` |  |
| `is_keyframe` | `"is_keyframe"` | `"boolean"` | `["number"]` |
| `loaded_frame_number` | `"loaded_frame_number"` | `"number"` |  |
| `max_blob_sz` | `"max_blob_sz"` | `"number"` |  |
| `read_frame` | `"read_frame"` | `"boolean"` | `["number"]` |
 

## Building Your Own Modules

### Requirements
* [emscripten](https://emscripten.org/docs/getting_started/index.html) is used to build the js modules from c code 

### Build
Run the `wasm/build.sh` script to build a new `vol_geom.mjs` file

## Repository Contents
TODO