// Manages a pool of Web Workers for transcoding .basis files.
//
// This manager abstracts the complexity of communicating with the Web Worker,
// providing a simple, Promise-based API for transcoding. It creates a worker,
// sends it data, and resolves a promise when the transcoded data is returned.
// This allows the main thread to offload heavy work without getting bogged down
// in the details of worker lifecycle and message passing.
//
// How it works:
// 1. Creates a new `vol_worker.js` instance.
// 2. Maintains a map of `pendingRequests` to track promises for ongoing jobs.
// 3. The `transcode` method sends a 'transcode' message to the worker, including
//    the raw .basis data and a unique ID for the request. It returns a Promise
//    that will be resolved or rejected later.
// 4. An `onmessage` handler listens for messages from the worker.
// 5. When a 'transcodeComplete' message arrives, it finds the corresponding
//    pending promise using the ID and resolves it with the transcoded data.
// 6. If a 'transcodeError' message is received, it rejects the promise.
// 7. Provides a `destroy` method to terminate the worker and clean up resources.

// workerUrl = 'vol_worker.js'
const TranscoderManager = () => {
    
    
    // Compute base URL of this script once, so we can find sibling files like 'vol_worker.js'.
	// Allows an explicit override via window.VOLOGRAM_PLAYERS_BASE_URL set in HTML before loading this file.
	var VOLOGRAM_PLAYERS_BASE_URL = (function () {
		try {
			if (typeof window !== "undefined" && window.VOLOGRAM_PLAYERS_BASE_URL) {
				return new URL(".", window.VOLOGRAM_PLAYERS_BASE_URL).href;
			}
		} catch (e) { /* no-op */ }
		var scripts = document.getElementsByTagName("script");
		for (var i = scripts.length - 1; i >= 0; i--) {
			var src = scripts[i].src || "";
			if (src.indexOf("/TranscoderManager.js") !== -1) {
				try { return new URL(".", src).href; } catch (e) { /* no-op */ }
			}
		}
		return document.baseURI || (typeof window !== "undefined" ? window.location.href : "");
	})();

    // In non-module environments, pass an absolute URL. Caller should resolve it.
    const worker = new Worker(new URL('vol_worker.js', VOLOGRAM_PLAYERS_BASE_URL).href);
    let nextRequestId = 1;
    const pendingRequests = new Map();

    // Handle messages coming back from the worker.
    worker.onmessage = (event) => {
        const { id, type, payload } = event.data;
        if (!pendingRequests.has(id)) {
            return; // Ignore if we don't have a pending request for this ID
        }

        if (type === 'log') {
            console.log(payload.message);
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
        // Reject all pending requests because the worker has crashed.
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