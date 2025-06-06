#include <emscripten.h>
#include <emscripten/wasmfs.h>
#include <stdio.h>
#include <string.h>

extern "C" {

/**
 * Setup OPFS filesystem using WasmFS
 * This creates a virtual directory "/opfs" that maps to the browser's Origin Private File System
 */
EMSCRIPTEN_KEEPALIVE
void setup_opfs_wasmfs() {
    printf("Setting up OPFS with WasmFS...\n");
    
    // Create OPFS backend
    auto opfs_backend = wasmfs_create_opfs_backend();
    
    // Create virtual directory "/opfs" and associate it with OPFS backend
    // Any writes to /opfs/filename will be stored in the browser's OPFS
    wasmfs_create_directory("opfs", 0777, opfs_backend);
    
    printf("OPFS setup complete. Files written to /opfs/ will be stored persistently.\n");
}

/**
 * Test OPFS functionality by writing and reading a test file
 */
EMSCRIPTEN_KEEPALIVE
int test_opfs_functionality() {
    printf("Testing OPFS functionality...\n");
    
    const char* test_path = "/opfs/test_file.txt";
    const char* test_content = "Hello from OPFS!";
    
    // Test write
    FILE* file = fopen(test_path, "w");
    if (!file) {
        printf("Error: Could not open file for writing: %s\n", test_path);
        return 0;
    }
    
    size_t written = fwrite(test_content, 1, strlen(test_content), file);
    fclose(file);
    
    if (written != strlen(test_content)) {
        printf("Error: Could not write complete content to file\n");
        return 0;
    }
    
    // Test read
    file = fopen(test_path, "r");
    if (!file) {
        printf("Error: Could not open file for reading: %s\n", test_path);
        return 0;
    }
    
    char buffer[256];
    size_t read_bytes = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    
    printf("Read from OPFS: %s\n", buffer);
    
    // Clean up test file
    if (remove(test_path) != 0) {
        printf("Warning: Could not remove test file\n");
    }
    
    if (strcmp(buffer, test_content) == 0) {
        printf("OPFS test successful!\n");
        return 1;
    } else {
        printf("OPFS test failed - content mismatch\n");
        return 0;
    }
}

/**
 * Check if a file exists in OPFS from WASM side
 * Useful for verifying sync operations
 */
EMSCRIPTEN_KEEPALIVE
int opfs_file_exists(const char* filename) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "/opfs/%s", filename);
    
    FILE* file = fopen(full_path, "r");
    if (file) {
        fclose(file);
        printf("File exists in WasmFS OPFS: %s\n", full_path);
        return 1;
    } else {
        printf("File not found in WasmFS OPFS: %s\n", full_path);
        return 0;
    }
}

/**
 * Get file size from OPFS
 */
EMSCRIPTEN_KEEPALIVE
long opfs_file_size(const char* filename) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "/opfs/%s", filename);
    
    FILE* file = fopen(full_path, "r");
    if (!file) {
        return -1;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    
    return size;
}

} 