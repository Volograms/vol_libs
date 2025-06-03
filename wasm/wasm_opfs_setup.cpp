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

} 