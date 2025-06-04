# Simple HTTP server for testing OPFS functionality
# Run this from the wasm directory: .\serve.ps1

$port = 8000
$url = "http://localhost:$port"

Write-Host "Starting HTTP server on $url" -ForegroundColor Green
Write-Host "Press Ctrl+C to stop the server" -ForegroundColor Yellow
Write-Host ""
Write-Host "Test URLs:" -ForegroundColor Cyan
Write-Host "  OPFS Test: $url/wasm/test-opfs.html" -ForegroundColor White
Write-Host "  Example OPFS:   $url/examples/opfs_streaming_example.html" -ForegroundColor White
Write-Host "  Example MEMFS:   $url/examples/05_vol_player_wasm/index_three.html" -ForegroundColor White
Write-Host ""

# Create HTTP listener
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add($url+"/")
$listener.Start()

# Flag to control the server loop - this gets set by Ctrl+C
$global:shouldStop = $false

try {
    while (-not $global:shouldStop) {
        try {
            # Start async context retrieval
            $contextResult = $listener.BeginGetContext($null, $null)
            
            # Poll every 500ms for either a request or Ctrl+C
            while (-not $contextResult.IsCompleted -and -not $global:shouldStop) {
                Start-Sleep -Milliseconds 500
                
                # Check if user pressed a key (including Ctrl+C)
                if ([Console]::KeyAvailable) {
                    $key = [Console]::ReadKey($true)
                    if ($key.Modifiers -eq [ConsoleModifiers]::Control -and $key.Key -eq [ConsoleKey]::C) {
                        Write-Host "`nReceived Ctrl+C, shutting down server..." -ForegroundColor Yellow
                        $global:shouldStop = $true
                        break
                    }
                }
            }
            
            # If we should stop, break out of the main loop
            if ($global:shouldStop) {
                break
            }
            
            # Get the context if available
            if ($contextResult.IsCompleted) {
                $context = $listener.EndGetContext($contextResult)
                $request = $context.Request
                $response = $context.Response
                
                # Get the requested file path
                $path = $request.Url.LocalPath
                if ($path -eq "/") { $path = "/wasm/test-opfs.html" }
                
                # Convert URL path to local file path
                $relativePath = $path.Replace("/", "\")
                $filePath = Join-Path (Get-Location) $relativePath
                
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
                        
                        # Set CORS headers for OPFS and SharedArrayBuffer support
                        $response.Headers.Add("Cross-Origin-Embedder-Policy", "require-corp")
                        $response.Headers.Add("Cross-Origin-Opener-Policy", "same-origin")
                        $response.Headers.Add("Access-Control-Allow-Origin", "*")
                        $response.Headers.Add("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
                        $response.Headers.Add("Access-Control-Allow-Headers", "Content-Type")
                        
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
        } catch [System.Net.HttpListenerException] {
            # This happens when the listener is stopped
            if ($_.Exception.ErrorCode -eq 995) {
                # ERROR_OPERATION_ABORTED - normal shutdown
                Write-Host "`nShutdown requested, stopping server..." -ForegroundColor Yellow
                break
            } else {
                Write-Host "HTTP Listener error: $($_.Exception.Message)" -ForegroundColor Red
                break
            }
        } catch [System.ObjectDisposedException] {
            # Listener was disposed, exit gracefully
            Write-Host "`nListener disposed, stopping server..." -ForegroundColor Yellow
            break
        } catch {
            # Handle other exceptions
            Write-Host "`nReceived interrupt, shutting down server..." -ForegroundColor Yellow
            break
        }
    }
} finally {
    # Cleanup
    try {
        $listener.Stop()
        $listener.Close()
    } catch {
        # Ignore cleanup errors
    }
    Write-Host "Server stopped." -ForegroundColor Yellow
} 