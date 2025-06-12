// Manages a pool of Web Workers for transcoding .basis files (ESM version).
//
// This manager abstracts the complexity of communicating with the Web Worker,
// providing a simple, Promise-based API for transcoding. It creates a worker,
// sends it data, and resolves a promise when the transcoded data is returned.
// This allows the main thread to offload heavy work without getting bogged down
// in the details of worker lifecycle and message passing.

const TranscoderManager = () => {
    const worker = new Worker(new URL('./vol_worker.mjs', import.meta.url), { type: 'module' });
    let nextRequestId = 1;
    const pendingRequests = new Map();

    // Handle messages coming back from the worker.
    worker.onmessage = (event) => {
        const { id, type, payload } = event.data;
        if (!pendingRequests.has(id)) {
            return;
        }

        const { resolve, reject } = pendingRequests.get(id);
        pendingRequests.delete(id);

        if (type === 'transcodeComplete') {
            resolve(payload);
        } else if (type === 'transcodeError') {
            reject(new Error(payload.message));
        }
    };

    // Handle any errors that might occur in the worker itself.
    worker.onerror = (error) => {
        console.error('TranscoderManager: Worker error:', error);
        pendingRequests.forEach(({ reject }) => {
            reject(new Error('Worker encountered an unrecoverable error.'));
        });
        pendingRequests.clear();
    };

    /**
     * Sends basis data to the worker for transcoding.
     * @param {Uint8Array} basisData - The raw .basis file data.
     * @param {number} format - The target GPU texture format.
     * @param {number} frameIndex - The index of the frame to be transcoded.
     * @returns {Promise<{transcodedData: Uint8Array, width: number, height: number}>}
     *          A promise that resolves with the transcoded texture data.
     */
    const transcode = (basisData, format, frameIndex) => {
        const id = nextRequestId++;
        const promise = new Promise((resolve, reject) => {
            pendingRequests.set(id, { resolve, reject });
        });

        worker.postMessage({
            id,
            type: 'transcode',
            payload: { basisData, format, frameIndex }
        }, [basisData.buffer]);

        return promise;
    };

    /**
     * Terminates the worker and cleans up resources.
     */
    const destroy = () => {
        worker.terminate();
        pendingRequests.forEach(({ reject }) => {
            reject(new Error('TranscoderManager was destroyed.'));
        });
        pendingRequests.clear();
    };

    return {
        transcode,
        destroy
    };
};

export default TranscoderManager; 