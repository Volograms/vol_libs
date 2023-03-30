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
            "https://cdn.jsdelivr.net/gh/Volograms/vol_libs@distribution-set-up/wasm/vol_geom.mjs"
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

## Using the Module
TODO

## Building Your Own Modules

### Requirements
* [emscripten](https://emscripten.org/docs/getting_started/index.html) is used to build the js modules from c code 

### Build
Run the `wasm/build.sh` script to build a new `vol_geom.mjs` file

## Repository Contents
TODO