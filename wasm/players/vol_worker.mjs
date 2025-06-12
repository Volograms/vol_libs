// Volograms .basis transcoding worker (ESM version)
//
// This worker script handles the CPU-intensive task of transcoding .basis texture files
// into a GPU-compatible format. By offloading this work from the main thread, it
// prevents the UI from freezing and ensures smooth playback, especially during
// the initial streaming and loading of a vologram.
//
// How it works:
// 1. Imports the main WebAssembly module ('vol_web.mjs').
// 2. Initializes the Basis Universal transcoder within its own Wasm instance.
// 3. Listens for 'transcode' messages from the main thread, which contain the raw
//    .basis file data and the target texture format.
// 4. When a message is received, it copies the data into the Wasm module's memory.
// 5. It calls the C function 'basis_transcode_v2' to perform the actual transcoding.
// 6. The resulting compressed texture data is then copied out of Wasm memory
//    and sent back to the main thread in a 'transcodeComplete' message.
// 7. Includes error handling to notify the main thread if transcoding fails.

import VolWeb from '../vol_web.mjs';

let wasmModule;

// Initializes the WebAssembly module and the Basis transcoder.
// This is called once when the worker is first created.
const initialize = async () => {
    const moduleArgs = {
        // Optimizations for worker environment
        noInitialRun: true,
        noExitRuntime: true
    };
    wasmModule = await VolWeb(moduleArgs);
    const success = wasmModule.ccall('basis_init', 'boolean');
    if (!success) {
        throw new Error('Worker: basis_init() failed');
    }
};

// Promise to ensure initialization is complete before handling messages.
const initializationPromise = initialize();

// Main message handler for the worker.
self.onmessage = async (event) => {
    const { id, type, payload } = event.data;

    if (type !== 'transcode') {
        return;
    }

    try {
        // Wait for Wasm module to be ready
        await initializationPromise;

        const { basisData, format, frameIndex } = payload;

        // Allocate memory inside the Wasm module for the input basis data.
        const dataPtr = wasmModule._malloc(basisData.length);
        if (!dataPtr) {
            throw new Error('Worker: malloc failed to allocate memory for basis data.');
        }
        wasmModule.HEAPU8.set(basisData, dataPtr);

        // Perform the transcoding by calling the C function.
        const success = wasmModule.ccall(
            'basis_transcode_v2',
            'boolean',
            ['number', 'number', 'number'],
            [format, dataPtr, basisData.length]
        );

        // Free the memory we allocated for the input data.
        wasmModule._free(dataPtr);

        if (!success) {
            throw new Error('Worker: basis_transcode_v2() failed');
        }

        // Retrieve the results of the transcoding.
        const resultPtr = wasmModule.ccall('basis_get_transcoded_ptr', 'number');
        const resultSize = wasmModule.ccall('basis_get_transcoded_sz_v2', 'number');
        const width = wasmModule.ccall('basis_get_transcoded_width_v2', 'number');
        const height = wasmModule.ccall('basis_get_transcoded_height_v2', 'number');

        if (resultSize === 0) {
            throw new Error('Worker: Transcoded data size is 0.');
        }
        
        // Copy the transcoded data out of Wasm memory into a new buffer.
        const transcodedData = new Uint8Array(wasmModule.HEAPU8.buffer, resultPtr, resultSize).slice();

        // Send the result back to the main thread.
        self.postMessage({
            id,
            type: 'transcodeComplete',
            payload: {
                transcodedData,
                width,
                height,
                frameIndex
            }
        }, [transcodedData.buffer]); // Transfer the buffer for efficiency

    } catch (error) {
        // If anything goes wrong, send an error message back.
        self.postMessage({
            id,
            type: 'transcodeError',
            payload: {
                message: error.message
            }
        });
    }
}; 