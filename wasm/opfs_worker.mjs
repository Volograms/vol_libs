/**
 * OPFS Worker for downloading and writing files using FileSystemSyncAccessHandle.
 * This runs in a separate thread to avoid blocking the main UI thread.
 */
let accessHandle;

// works in iOS but it is blocking wasmfs and preventing reading
// because wasmfs doesn't support "readwrite-unsafe" yet
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
			// if (!headerFlushed && bytesReceived > 10048576) {
			// 	accessHandle.flush();
			// 	self.postMessage({ type: "header_ready" });
			// 	headerFlushed = true;
			// }
		}

		// Final flush and completion message
		accessHandle.flush();
        accessHandle.close();
        self.postMessage({ type: "header_ready" });
		self.postMessage({ type: "download_complete" });
	} catch (error) {
		self.postMessage({ type: "error", error: error.message });
	} finally {
		if (accessHandle) {
			accessHandle.close();
		}
	}
}; 

// // does not work in iOS and it doesn't flush data until close() is called
// // this works but is extremely slow because every createWritable makes its own a copy of an existing file 
// self.onmessage = async (event) => {
// 	const { fileUrl, destinationPath } = event.data;

// 	if (!fileUrl || !destinationPath) {
// 		self.postMessage({ error: "Missing fileUrl or destinationPath" });
// 		return;
// 	}
//     const destinationPath1 = "/opfs/test.vols";

// 	try {
// 		const opfsRoot = await navigator.storage.getDirectory();
// 		const fileHandle = await opfsRoot.getFileHandle(destinationPath.replace("/opfs/", ""), { create: true });
// 		accessHandle = await fileHandle.createWritable({keepExistingData: false, mode: "exclusive"});

// 		const response = await fetch(fileUrl);
// 		if (!response.ok) {
// 			throw new Error(`HTTP error! Status: ${response.status}`);
// 		}
//         await accessHandle.close();

// 		const fileSize = +response.headers.get("content-length");
// 		const reader = response.body.getReader();
// 		let bytesReceived = 0;
// 		let headerFlushed = false;

// 		while (true) {
// 			const { done, value } = await reader.read();
// 			if (done) {
// 				break;
// 			}
//             accessHandle = await fileHandle.createWritable({keepExistingData: true, mode: "exclusive"});
//             accessHandle.write({ type: "write", position: bytesReceived, data: value });
// 			bytesReceived += value.length;
//             await accessHandle.close();

// 			// Post progress updates back to the main thread
// 			if (fileSize) {
// 				self.postMessage({ type: "progress_update", progress: bytesReceived / fileSize });
// 			}

// 			// After downloading ~1MB, flush the data and notify the main thread
// 			// that the header is ready.
// 			if (!headerFlushed && bytesReceived > 10048576) {
// 				self.postMessage({ type: "header_ready" });
// 				headerFlushed = true;
// 			}
// 		}
//         await accessHandle.close();
//         self.postMessage({ type: "header_ready" });
// 		self.postMessage({ type: "download_complete" });
// 	} catch (error) {
// 		self.postMessage({ type: "error", error: error.message });
// 	} finally {
// 		if (accessHandle) {
// 			await accessHandle.close();
// 		}
// 	}
// }; 