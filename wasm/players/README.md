# Vologram Players

This directory contains player components that work with both standard and OPFS versions of the VolWeb library.

## VologramPlayer Usage

The `VologramPlayer` now accepts the VolWeb module as its first parameter, giving you full control over which variant to use.

### Using with Standard Version

```javascript
import VolWebStandard from '@volograms/web_vol_lib/standard';
import { VologramPlayer } from '@volograms/web_vol_lib/players/VologramPlayer.mjs';

// Create player with standard VolWeb (memory-based storage)
const player = VologramPlayer(VolWebStandard, [/* extensions */]);

await player.open({
    headerUrl: 'path/to/header.vols',
    sequenceUrl: 'path/to/sequence.vols',
    useOPFS: false  // Uses memory storage
});
```

### Using with OPFS Version

```javascript
import VolWebOPFS from '@volograms/web_vol_lib/opfs';
import { VologramPlayer } from '@volograms/web_vol_lib/players/VologramPlayer.mjs';

// Create player with OPFS VolWeb (disk-based storage)
const player = VologramPlayer(VolWebOPFS, [/* extensions */]);

await player.open({
    headerUrl: 'path/to/header.vols',
    sequenceUrl: 'path/to/sequence.vols',
    useOPFS: true  // Uses persistent disk storage
});
```

### Available Extensions

- `ThreeJsPlayerExtension` - Integration with Three.js
- `WebGlPlayerExtension` - Direct WebGL rendering

### Migration from Previous Versions

**Old API (deprecated):**
```javascript
const player = VologramPlayer([extensions]);
```

**New API:**
```javascript
import VolWeb from '@volograms/web_vol_lib'; // or /standard or /opfs
const player = VologramPlayer(VolWeb, [extensions]);
```

## Benefits of the New API

1. **Flexibility** - Choose between memory-based or OPFS-based storage (OPFS version uses Threads and requires CORS headers to be set)
2. **Performance** - OPFS version allows to use large files on mobile devices (especially iOS)
3. **Explicit Dependencies** - Clear control over which WebAssembly variant is used