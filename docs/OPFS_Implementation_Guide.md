# OPFS Implementation Guide for Volumetric Video Streaming

## Overview

We have successfully implemented **OPFS (Origin Private File System)** support for your volumetric video library using **WasmFS OPFS backend**. This solves the memory limitations you were experiencing on mobile devices, especially iOS, by storing files on disk instead of in memory.

## ✅ What's Been Implemented

### 1. WasmFS OPFS Backend (`wasm/wasm_opfs_setup.cpp`)
- **`setup_opfs_wasmfs()`** - Creates WasmFS OPFS backend and virtual `/opfs/` directory
- **`test_opfs_functionality()`** - Tests file write/read operations through WasmFS
- **Virtual Filesystem**: `/opfs/` directory maps directly to browser's actual OPFS storage
- **Standard C API**: Use normal `fopen("/opfs/filename", "w")` operations

### 2. JavaScript Integration (`wasm/pre.js`)
- **`Module.initOPFS()`** - Check browser OPFS support
- **`Module.setupOPFS()`** - Call C++ backend setup via ccall
- **`Module.isOPFSReady()`** - Check if OPFS backend is ready
- **No Direct OPFS API**: Implementation doesn't use `navigator.storage.getDirectory()`

### 3. Player Integration (`wasm/players/VologramPlayer.js`)
- Added `useOPFS` parameter to the `.open()` function
- Automatic fallback to MEMFS if OPFS is not supported
- Dynamic file path handling (`/opfs/filename` vs `filename`)
- Seamless integration with existing C library

### 4. Build Configuration (`wasm/build-js.bat`)
- Updated with WasmFS flags (`-s WASMFS=1`, `-s FORCE_FILESYSTEM=1`)
- pthread support for WasmFS OPFS backend
- Proper function exports for setup functions

## 🚀 How OPFS Actually Works

### Technical Architecture

```
Your C Code → WasmFS Virtual FS → Browser's OPFS Storage
     ↓              ↓                    ↓
fopen("/opfs/     /opfs/ directory    Persistent disk
file.vols")      maps to OPFS         storage
```

### Implementation Details

1. **WasmFS Backend Creation**: `wasmfs_create_opfs_backend()` creates bridge to browser OPFS
2. **Virtual Directory**: `wasmfs_create_directory("opfs", 0777, opfs_backend)` creates `/opfs/` mount point
3. **File Operations**: Standard C file ops (`fopen`, `fwrite`, `fread`) work transparently
4. **Automatic Routing**: WasmFS routes `/opfs/` paths to browser's actual OPFS storage
5. **Persistence**: Files persist across browser sessions

### Key Differences from Direct OPFS API

| Aspect | Direct JavaScript OPFS | WasmFS OPFS Backend |
|--------|----------------------|-------------------|
| **Access Method** | `navigator.storage.getDirectory()` | `fopen("/opfs/file", "w")` |
| **Integration** | Complex async JavaScript | Simple C file operations |
| **Code Changes** | Extensive JS modifications | Minimal - just path changes |
| **Performance** | Good but complex | Excellent and simple |
| **C Library Compat** | Requires major changes | Drop-in replacement |

## 🚀 How to Use OPFS Mode

### Basic Usage

```javascript
// Traditional MEMFS (memory-based)
const player = VologramPlayer();
await player.open({
    sequenceUrl: "vologram.vols",
    useOPFS: false  // or omit this parameter
});

// New OPFS mode (disk-based)  
const player = VologramPlayer();
await player.open({
    sequenceUrl: "large_vologram.vols",
    useOPFS: true  // Enable OPFS storage
});
```

### Advanced Usage with Streaming

```javascript
const player = VologramPlayer();

// For single file volograms
await player.open({
    sequenceUrl: "large_vologram.vols",
    useOPFS: true
}, (progress) => {
    console.log(`Download progress: ${Math.round(progress * 100)}%`);
});

// For split header/sequence files
await player.open({
    headerUrl: "header.vols",
    sequenceUrl: "sequence.vols", 
    useOPFS: true
}, (progress) => {
    console.log(`Download progress: ${Math.round(progress * 100)}%`);
});
```

## 🧪 Testing Your Implementation

### 1. Browser Compatibility Test
Open `wasm/test-opfs.html` in different browsers to verify OPFS support:
- ✅ Chrome 86+
- ✅ Firefox 111+  
- ✅ Safari 15.2+
- ❌ Older browsers (auto-fallback to MEMFS)

### 2. Practical Testing  
Use `examples/opfs_streaming_example.html` to test with real vologram files:
1. Enter your vologram URL
2. Select OPFS or MEMFS mode
3. Compare memory usage and performance

### 3. Mobile Device Testing
Test on iOS/Android devices with large volumetric video files:
```javascript
// Test large file streaming on mobile
const player = VologramPlayer();
await player.open({
    sequenceUrl: "https://your-server.com/large_vologram.vols",
    useOPFS: true  // Should prevent memory issues
});
```

## 🔧 Integration Steps

### 1. Update Your Current Code
Replace your existing player initialization:

```javascript
// OLD - MEMFS only
vologramPlayer.open({
    sequenceUrl: fileUrl
});

// NEW - With OPFS option
vologramPlayer.open({
    sequenceUrl: fileUrl,
    useOPFS: true  // Add this for large files/mobile
});
```

### 2. Your C Code Automatically Works
```c
// This works exactly the same, just with /opfs/ prefix
FILE* file = fopen("/opfs/sequence.vols", "rb");
if (file) {
    // Normal file operations work transparently
    fread(buffer, 1, size, file);
    fclose(file);
}
```

### 3. No JavaScript OPFS API Needed
```javascript
// ❌ NOT NEEDED - No direct OPFS API calls required
// const root = await navigator.storage.getDirectory();
// const fileHandle = await root.getFileHandle('file.vols');

// ✅ INSTEAD - Just use the player with useOPFS flag
await player.open({ sequenceUrl: "file.vols", useOPFS: true });
```

## 📱 Mobile Device Benefits

### Before (MEMFS)
- ❌ Large files cause memory errors on iOS
- ❌ Browser crashes with multi-GB volumetric videos
- ❌ Limited to smaller file sizes

### After (OPFS)
- ✅ Large files stored on disk, not memory
- ✅ Better performance on mobile devices
- ✅ Support for multi-GB volumetric videos
- ✅ Automatic fallback for unsupported browsers

## 🛠️ Troubleshooting

### Issue: "OPFS not supported"
**Solution:** Use modern browser or fallback automatically occurs
```javascript
// The library automatically handles this
await player.open({
    sequenceUrl: "vologram.vols",
    useOPFS: true  // Will fallback to MEMFS if not supported
});
```

### Issue: Build errors
**Solution:** Ensure you have the latest Emscripten version with OPFS support
```bash
emcc --version  # Should be recent version
```

### Issue: Files not found in C code
**Solution:** Verify file paths are correct
```javascript
// OPFS files are accessed at /opfs/ prefix in C code
// This is handled automatically by the player
```

### Issue: Performance testing
**Solution:** Use browser developer tools to monitor memory usage
```javascript
// Compare memory usage between MEMFS and OPFS modes
console.log('Memory before:', performance.memory.usedJSHeapSize);
// Load vologram...
console.log('Memory after:', performance.memory.usedJSHeapSize);
```

## 🔍 Technical Details

### File System Architecture
```
WasmFS Virtual Filesystem:     Browser OPFS Storage:
┌─────────────────────┐       ┌─────────────────────┐
│ /opfs/vologram.vols │  →    │ [OPFS] vologram.vols│
│ /opfs/header.vols   │  →    │ [OPFS] header.vols  │  
│ /opfs/sequence.vols │  →    │ [OPFS] sequence.vols│
└─────────────────────┘       └─────────────────────┘
```

### Streaming Flow
1. **JavaScript**: Downloads data chunks via fetch
2. **WasmFS**: Routes `/opfs/` file operations to OPFS backend  
3. **OPFS Backend**: Writes chunks directly to browser's OPFS storage
4. **C Library**: Accesses files via standard `fopen("/opfs/filename.vols")`
5. **Transparency**: Your C code doesn't know it's using OPFS

## 📈 Performance Comparison

| Aspect | MEMFS | WasmFS OPFS Backend |
|--------|-------|-------------------|
| **Storage Location** | Browser Memory | Browser Disk (via WasmFS) |
| **C API Compatibility** | Perfect | Perfect |
| **JavaScript Changes** | None | Minimal |
| **File Size Limit** | ~RAM available | ~Disk space available |
| **Mobile Performance** | Poor (crashes) | Excellent |
| **Persistence** | Session only | Persistent |
| **Browser Support** | All browsers | Modern browsers |
| **Memory Usage** | High | Low |
| **Implementation Complexity** | Simple | Simple (WasmFS handles complexity) |

## 🎯 Recommended Usage

### Use OPFS When:
- ✅ Working with large volumetric video files (>100MB)
- ✅ Targeting mobile devices
- ✅ Users have modern browsers
- ✅ Memory efficiency is important

### Use MEMFS When:
- ✅ Small files (<50MB)  
- ✅ Need maximum browser compatibility
- ✅ Temporary/session-only storage preferred

## 📝 Next Steps

1. **Test with your volumetric video files** using the provided examples
2. **Update your production code** to use `useOPFS: true` for large files
3. **Monitor mobile device performance** and memory usage
4. **Consider making OPFS the default** for new implementations

Your implementation is now ready to handle large volumetric video files efficiently, especially on mobile devices where memory constraints were previously causing issues!

---

## 🆘 Need Help?

If you encounter issues:
1. Check browser console for OPFS-related errors
2. Use the test pages to verify functionality
3. Compare MEMFS vs OPFS performance in your use case
4. Monitor memory usage in browser developer tools

The implementation maintains backward compatibility, so existing code will continue to work while new code can benefit from OPFS storage. 