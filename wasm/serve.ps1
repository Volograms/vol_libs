# Simple HTTP server for testing OPFS functionality
# Run this from the wasm directory: .\serve.ps1

$port = 8000
$url = "http://localhost:$port/"

Write-Host "Starting HTTP server on $url" -ForegroundColor Green
Write-Host "Press Ctrl+C to stop the server" -ForegroundColor Yellow
Write-Host ""
Write-Host "Test URLs:" -ForegroundColor Cyan
Write-Host "  OPFS Test: $url/test-opfs.html" -ForegroundColor White
Write-Host "  Example:   $url/../examples/opfs_streaming_example.html" -ForegroundColor White
Write-Host ""

# Create HTTP listener
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add($url)
$listener.Start()

try {
    while ($true) {
        # Wait for a request
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response
        
        # Get the requested file path
        $path = $request.Url.LocalPath
        if ($path -eq "/") { $path = "/test-opfs.html" }
        
        # Convert URL path to local file path
        $filePath = Join-Path (Get-Location) $path.Replace("/", "\")
        
        Write-Host "$(Get-Date -Format 'HH:mm:ss') - $($request.HttpMethod) $path" -ForegroundColor Gray
        
        try {
            if (Test-Path $filePath -PathType Leaf) {
                # Serve the file
                $content = [System.IO.File]::ReadAllBytes($filePath)
                
                # Set appropriate content type
                $extension = [System.IO.Path]::GetExtension($filePath).ToLower()
                switch ($extension) {
                    ".html" { $response.ContentType = "text/html; charset=utf-8" }
                    ".js"   { $response.ContentType = "application/javascript; charset=utf-8" }
                    ".mjs"  { $response.ContentType = "application/javascript; charset=utf-8" }
                    ".css"  { $response.ContentType = "text/css; charset=utf-8" }
                    ".json" { $response.ContentType = "application/json; charset=utf-8" }
                    ".wasm" { $response.ContentType = "application/wasm" }
                    default { $response.ContentType = "application/octet-stream" }
                }
                
                # Set CORS headers for OPFS
                $response.Headers.Add("Cross-Origin-Embedder-Policy", "require-corp")
                $response.Headers.Add("Cross-Origin-Opener-Policy", "same-origin")
                
                $response.ContentLength64 = $content.Length
                $response.OutputStream.Write($content, 0, $content.Length)
            } else {
                # File not found
                $response.StatusCode = 404
                $response.ContentType = "text/plain"
                $notFound = [System.Text.Encoding]::UTF8.GetBytes("File not found: $path")
                $response.ContentLength64 = $notFound.Length
                $response.OutputStream.Write($notFound, 0, $notFound.Length)
                Write-Host "  404 - File not found: $filePath" -ForegroundColor Red
            }
        } catch {
            # Server error
            $response.StatusCode = 500
            $response.ContentType = "text/plain"
            $errorMsg = [System.Text.Encoding]::UTF8.GetBytes("Server error: $($_.Exception.Message)")
            $response.ContentLength64 = $errorMsg.Length
            $response.OutputStream.Write($errorMsg, 0, $errorMsg.Length)
            Write-Host "  500 - Error: $($_.Exception.Message)" -ForegroundColor Red
        } finally {
            $response.Close()
        }
    }
} finally {
    $listener.Stop()
    Write-Host "Server stopped." -ForegroundColor Yellow
} 