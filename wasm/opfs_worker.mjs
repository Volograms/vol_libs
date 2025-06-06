/**
 * OPFS Worker for downloading and writing files using FileSystemSyncAccessHandle.
 * This runs in a separate thread to avoid blocking the main UI thread.
 */
let accessHandle;

self.onmessage = async (event) => {
	const { fileUrl, destinationPath } = event.data;

	if (!fileUrl || !destinationPath) {
		self.postMessage({ error: "Missing fileUrl or destinationPath" });
		return;
	}

	try {
		const opfsRoot = await navigator.storage.getDirectory();
		const fileHandle = await opfsRoot.getFileHandle(destinationPath.replace("/opfs/", ""), { create: true });
		accessHandle = await fileHandle.createSyncAccessHandle({mode: "readwrite-unsafe"});

		const response = await fetch(fileUrl);
		if (!response.ok) {
			throw new Error(`HTTP error! Status: ${response.status}`);
		}

		const fileSize = +response.headers.get("content-length");
		const reader = response.body.getReader();
		let bytesReceived = 0;
		let headerFlushed = false;

		while (true) {
			const { done, value } = await reader.read();
			if (done) {
				break;
			}

			accessHandle.write(value, { at: bytesReceived });
			bytesReceived += value.length;

			// Post progress updates back to the main thread
			if (fileSize) {
				self.postMessage({ type: "progress_update", progress: bytesReceived / fileSize });
			}

			// After downloading ~1MB, flush the data and notify the main thread
			// that the header is ready.
			if (!headerFlushed && bytesReceived > 10048576) {
				accessHandle.flush();
				self.postMessage({ type: "header_ready" });
				headerFlushed = true;
			}
		}

		// Final flush and completion message
		accessHandle.flush();
		self.postMessage({ type: "download_complete" });
	} catch (error) {
		self.postMessage({ type: "error", error: error.message });
	} finally {
		if (accessHandle) {
			accessHandle.close();
		}
	}
}; 