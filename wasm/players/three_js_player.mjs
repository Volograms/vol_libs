import * as THREE from "three";

var Module = {};
var scene;
var renderer;
var video;
const video_tag = "texture_video_el";
export var vologram = new Object();   // Stores state about the current vologram.

vologram.fps = 30.0;           // Change this value if not using 30 frames-per-second video.
var init_done = false;         // If the basic shaders/objects are set up. A vologram can be downloaded & prepared after this is true.

var texture_width, texture_height = -1;
var spare_texure;

function create_vologram_geometry() {
    vologram.geometry = new THREE.BufferGeometry();
}

function create_vologram_texture() {
    console.debug( "Creating a texture with video: ", video );
    vologram.texture = new THREE.VideoTexture( video );
    console.debug(vologram.texture);
}

function create_vologram_mesh() {
    /*
    You can change the shader or material of the
    vologram here.

    For reference: 
    https://threejs.org/docs/#api/en/materials/ShaderMaterial
    
    You can call similar code to change the material of the
    mesh at runtime:
    `vologram.mesh.material = new THREE.MeshBasicMaterial(...)`
    */
    vologram.material = new THREE.MeshBasicMaterial({
        color: 0xffffff,
        map: vologram.texture
    });
    vologram.mesh = new THREE.Mesh(vologram.geometry, vologram.material);
    vologram.mesh.position.set( 0, 0, 0 )
    // The vologram is mirrored
    vologram.mesh.scale.set( -1, 1, 1 )
    // Look at the camera 
    //vologram.mesh.rotation.set( 0, Math.PI, 0 )
}

function create_vologram() {
    // It is important to create the geometry and texture before the mesh 
    create_vologram_geometry();
    create_vologram_texture();
    create_vologram_mesh();
    scene.add( vologram.mesh );
}

function init_video() {
    if (!video) {
        video = document.createElement('video');
        video.setAttribute("controls", "");
        video.setAttribute("id", video_tag);
        video.setAttribute("type", "video/mp4;");
        video.setAttribute("width", "0");
        video.setAttribute("height", "0");
        video.setAttribute("loop", "");
        video.setAttribute("playsinline", "");
        document.body.prepend( video );
    }
    
    video.addEventListener( "loadedmetadata", function (e) {
        console.log( e );
        console.log( e.target.videoWidth, e.target.videoHeight );
        // target.videoHeight
        console.log(vologram.texture.format);
        texture_width = e.target.videoWidth;
        texture_height = e.target.videoHeight;
        const data = new Uint8Array(texture_width * texture_height * 3);
        spare_texure = new THREE.DataTexture(data, texture_width, texture_height, vologram.texture.format);
        //spare_texure.flipY = true;
        vologram.mesh.material = new THREE.MeshBasicMaterial({
            color: 0xffffff,
            map: spare_texure,
        });

    }, false );
    
}

function init() {
    vologram.n_indices = 0;
    vologram.has_normals = false;
    vologram.last_frame_loaded = -1;    // To avoid reloading the same frame.
    vologram.last_keyframe_loaded = -1; // Records if a preceding keyframe was already loaded.
    vologram.header_ready = false;      // If file is downloaded.
    vologram.sequence_ready = false;    // If file is downloaded.
    vologram.ready = false;             // If data is downloaded and vologram is ready to play.
    
    init_done = true;

    return true;
}

Module['onRuntimeInitialized'] = function() {
    console.log('WASM for VOL_GEOM started...');

    init();
    if (!init) { return false; }

    console.log("render starting...");
}

// Copy vol_geom frame `frame_idx` into vologram webgl mesh.
// Returns true on success, and false on error loading frame.
// This function assumes that any preceding, required, keyframe has been resolved and loaded already.
function mesh_from_frame(frame_idx) {
    if (!vologram.geometry) { return; }
    if (vologram.last_frame_loaded == frame_idx) { return; } // Safety catch to avoid reloading the same frame twice.

    if (spare_texure && renderer && vologram.texture) {
        renderer.copyTextureToTexture(
            new THREE.Vector2(0, 0),
            vologram.texture,
            spare_texure
        );
        spare_texure.needsUpdate = true;
        // vologram.mesh.material.map = spare_texure;
    }

    // Ask the vol_geom WASM to read the frame data from the vologram file into `_frame_data`.
    var ret = Module.ccall('read_frame', 'boolean', ['number'], [frame_idx]);
    if (!ret) { return false; }

    var is_key = Module.ccall('is_keyframe', 'boolean', ['number'], [frame_idx]);

    // Positions - fetch and upload.
    var vp_copied = Module.ccall('frame_vp_copied', 'number');
    var vp_sz = Module.ccall('frame_vertices_sz', 'number');
    var vp_f32 = new Float32Array(Module.HEAP8.buffer, vp_copied, vp_sz / 4);
    vologram.geometry.setAttribute( 'position', new THREE.Float32BufferAttribute(vp_f32, 3 ));

    if (vologram.has_normals) { // Not all volograms include normals.
        // Normals - fetch and upload.
        var normals_copied = Module.ccall('frame_normals_copied', 'number');
        var normals_sz = Module.ccall('frame_normals_sz', 'number');
        var vn_f32 = new Float32Array(Module.HEAP8.buffer, normals_copied, normals_sz / 4);
        vologram.geometry.setAttribute( 'normal', new THREE.Float32BufferAttribute(vn_f32, 3 ));
    }

    // Key-Frames also contain texture coordinate and index data.
    if (is_key) {
        vologram.last_keyframe_loaded = frame_idx;

        // Texture Coordinates - fetch and upload.
        var uvs_copied = Module.ccall('frame_uvs_copied', 'number');
        var uvs_sz = Module.ccall('frame_uvs_sz', 'number');
        var uvs_f32 = new Float32Array(Module.HEAP8.buffer, uvs_copied, uvs_sz / 4);
        vologram.geometry.setAttribute( 'uv', new THREE.Float32BufferAttribute( uvs_f32, 2 ));

        // Indices - fetch and upload.
        var indices_copied = Module.ccall('frame_indices_copied', 'number');
        var indices_sz = Module.ccall('frame_i_sz', 'number');
        vologram.n_indices = indices_sz / 2; // ushort
        var indices_u16 = new Uint16Array(Module.HEAP8.buffer, indices_copied, vologram.n_indices);
        vologram.geometry.setIndex( new THREE.Uint16BufferAttribute( indices_u16, 1 ))
    }

    // It seems that calculating bounding sphere does not work and always returns NaN value for the radius
    if (vologram.geometry.boundingSphere === null)
        vologram.geometry.boundingSphere = new THREE.Sphere();
    if (
        vologram.geometry.boundingSphere.radius <= 0 ||
        isNaN(vologram.geometry.boundingSphere.radius)
    ) {
        vologram.geometry.computeBoundingBox();
        vologram.geometry.boundingBox.getBoundingSphere(vologram.geometry.boundingSphere);
    }

    vologram.last_frame_loaded = frame_idx;
    return true;
}

function start_video() {
    video.src = vologram.video_url;
    video.requestVideoFrameCallback(doSomethingWithTheFrame);
    video.play();
}

function init_vologram() {
    if (!vologram.header_ready || !vologram.sequence_ready || vologram.ready) { return; } // Wait until both files are downloaded.
    var ret = Module.ccall('create_file_info', 'boolean', ['string', 'string'], ['header.vols', 'sequence_0.vols']);
    console.log('create_file_info = ' + ret);
    if (!ret) {
        console.error("failed to load vologram");
        return;
    }
    vologram.has_normals = Module.ccall('has_normals', 'boolean');
    vologram.max_blob_size = Module.ccall('max_blob_sz', 'number')
    console.log( "max blob size =", vologram.max_blob_size );
    
    console.log("vologram.has_normals = " + vologram.has_normals);
    mesh_from_frame(0); // Load in frame 0 so something displays even if we don't play yet.
    console.log("mesh initialised");
    start_video();
    vologram.ready = true;
}

// This can be called to add an artificial slow-down in the code. Parameter value is in micro-seconds.
function test_usleep(us) {
    Module.ccall('do_usleep', 'number', ['number'], [us]);
}

// Calls mesh_from_frame() but first loads a keyframe, if required.
function update_mesh_frame_allowing_skip(desired_frame_index) {
    // int find_previous_keyframe( int frame_idx );
    var keyframe_required = Module.ccall('find_previous_keyframe', 'number', ['number'], [desired_frame_index]);

    // If running slowly we may skip over a keyframe. Grab that now to avoid stale keyframe desync.
    if (vologram.last_keyframe_loaded != keyframe_required) { mesh_from_frame(keyframe_required); }
    // Load actual current frame.
    mesh_from_frame(desired_frame_index)
}

const doSomethingWithTheFrame = (now, metadata) => {
    var frame_index = Math.floor(metadata.mediaTime * vologram.fps);
    update_mesh_frame_allowing_skip(frame_index);
    if (init_done)
        video.requestVideoFrameCallback(doSomethingWithTheFrame); // Re-register the callback to be notified about the next frame.
}

// Function to download files and start playback. Video playback needs to start with a click.
export function start() {
    var FS = Module.FS;
    if (init_done) {
        { // Header file
            console.log("filename=" + vologram.header_url);
            var xmlhttp = new XMLHttpRequest();
            xmlhttp.open("GET", vologram.header_url, true);
            xmlhttp.responseType = "arraybuffer"; // Binary file type. Note: Can be a "blob" or "arraybuffer".
            xmlhttp.onload = (event) => {
                const array_buffer = xmlhttp.response; // Note: not xmlhttp.responseText
                if (array_buffer) {
                    const byte_array = new Uint8Array(array_buffer);
                    console.log("file fetched = " + xmlhttp.responseURL + " bytes = " + byte_array.length);
                    // NOTE! if filename has a path in it this function will fail if we don't mkdir first
                    var stream = FS.open("header.vols", 'w');
                    FS.write(stream, byte_array, 0, byte_array.length, 0);
                    FS.close(stream);
                    console.log("file stored in vfs as = " + "header.vols");
                    vologram.header_ready = true;
                    init_vologram(); // Attempt init. Since files are downloaded asynchronously we don't know if sequence was downloaded first.
                }
            };
            xmlhttp.send(null);
        } // endblock header file.
        { // Sequence file
            console.log("filename=" + vologram.sequence_url);
            var seq = new XMLHttpRequest();
            seq.open("GET", vologram.sequence_url, true);
            seq.responseType = "arraybuffer"; // binary file type. NOTE! Can be a "blob" or "arraybuffer"
            seq.onload = (event) => {
                const array_buffer = seq.response; // Note: not xmlhttp.responseText
                if (array_buffer) {
                    const byte_array = new Uint8Array(array_buffer);
                    console.log("file fetched = " + seq.responseURL + " bytes = " + byte_array.length);
                    var stream = FS.open("sequence_0.vols", 'w');
                    FS.write(stream, byte_array, 0, byte_array.length, 0);
                    FS.close(stream);
                    console.log("file stored in vfs as = " + "sequence_0.vols");
                    vologram.sequence_ready = true;
                    init_vologram(); // Attempt init. Since files are downloaded asynchronously we don't know if header was downloaded first.
                }
            };
            seq.send(null);
        } // endblock sequence file.
    } // endif init_done
} // endfunction load_clicked()

export function create(scene_3js, renderer_3js, header_url, sequence_url, video_url, initModule, video_element = null) {
    scene = scene_3js;
    renderer = renderer_3js;
    vologram.header_url = header_url;
    vologram.sequence_url = sequence_url;
    vologram.video_url = video_url;
    video = video_element;
    init_video();
    create_vologram();
    initModule(Module);
}

export function close() {
    if (video) {
        video.pause();
        video.remove();
    }
    if (vologram) {
        vologram.mesh = null;
        vologram.material = null;
        vologram.geometry = null;
        vologram.texture = null;
    }
    Module.ccall('free_file_info', 'boolean');
    init_done = false;
}
