<#
.SYNOPSIS
    Runs clang-tidy on all project files using compile_commands.json.
.DESCRIPTION
    Recursively scans src and include folders for .cpp/.h/.hpp files,
    excludes external or third-party folders, and runs clang-tidy with fixes or dry-run.
.PARAMETER BuildDir
    Optional. Path to the folder containing compile_commands.json. Auto-detected if omitted.
.PARAMETER Mode
    Optional. "fix" to apply fixes, "dry" to only report issues. Default: "fix"
#>

param(
    [string]$BuildDir,
    [ValidateSet("fix","dry")]
    [string]$Mode = "fix"
)

# -------------------------
# Auto-detect compile_commands.json if BuildDir not specified
# -------------------------
if (-not $BuildDir) {
    Write-Host "Auto-detecting build folder with compile_commands.json..."
    $BuildDir = Get-ChildItem -Path build -Recurse -Directory | Where-Object {
        Test-Path (Join-Path $_.FullName "compile_commands.json")
    } | Select-Object -First 1

    if ($null -eq $BuildDir) {
        Write-Error "No build folder with compile_commands.json found. Please specify -BuildDir"
        exit 1
    } else {
        $BuildDir = $BuildDir.FullName
        Write-Host "Found build folder: $BuildDir"
    }
}

# -------------------------
# Find all source & header files
# -------------------------
$folders = @("src","include")
$excludePatterns = @("\\external\\","\\third_party\\")

$files = @()
foreach ($f in $folders) {
    if (Test-Path $f) {
        $files += Get-ChildItem -Path $f -Recurse -Include *.cpp,*.h,*.hpp
    }
}

# Exclude unwanted folders
foreach ($pattern in $excludePatterns) {
    $files = $files | Where-Object { $_.FullName -notmatch $pattern }
}

if ($files.Count -eq 0) {
    Write-Host "No source files found!"
    exit 0
}

# -------------------------
# Run clang-tidy
# -------------------------
$fixArg = ""
if ($Mode -eq "fix") { $fixArg = "-fix" }

Write-Host "Running clang-tidy in $Mode mode..."
$counter = 0

foreach ($file in $files) {
    $counter++
    Write-Host "[$counter/$($files.Count)] $($file.FullName)"
    clang-tidy $file.FullName -p $BuildDir --header-filter=".*" $fixArg
}

Write-Host "`nclang-tidy finished. Processed $counter files."
