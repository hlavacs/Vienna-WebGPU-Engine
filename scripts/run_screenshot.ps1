# Vienna-WebGPU-Engine: launch an example, grab a focused window screenshot,
# kill the process, and report stderr size. Reused by Claude during iteration
# so it doesn't have to redefine the P/Invoke block + capture math each turn.
#
# Usage:
#   pwsh -File scripts/run_screenshot.ps1 -Example main_demo `
#        -Out  seakeep_check.png -WaitSeconds 12 -InitialScene SeaKeep
#
# Notes:
#  * Screenshots only capture the desktop. They are NOT a faithful preview of
#    what the user sees - the user may have moved the orbit camera, the demo
#    may load assets asynchronously, the day-night cycle may differ. ALWAYS
#    confirm with the user (AskUserQuestion) whether the screenshot matches
#    their experience before iterating on the rendering pipeline based on it.

[CmdletBinding()]
param(
    [string]$Example     = "main_demo",
    [string]$Out         = "screenshot.png",
    [int]$WaitSeconds    = 8,
    [string]$InitialScene = "",    # if set, edits main.cpp to loadScene(InitialScene)
                                   # and reverts to "Demo" afterwards
    [string]$BuildType   = "Debug",
    [string]$Backend     = "WGPU"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$exeDir   = Join-Path $repoRoot "examples\build\$Example\Windows\$BuildType"
$exePath  = Join-Path $exeDir   "$($Example -replace '_(.)', { $_.Groups[1].Value.ToUpper() })".Replace($Example, "MainDemo") + ".exe"
# Fallback: just look for any *.exe in that directory if the name guess fails
if (-not (Test-Path $exePath)) {
    $cand = Get-ChildItem -Path $exeDir -Filter "*.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cand) { $exePath = $cand.FullName }
}
if (-not (Test-Path $exePath)) {
    throw "Executable not found in $exeDir. Did you run scripts\build-example.bat $Example $BuildType $Backend ?"
}

$mainCpp = Join-Path $repoRoot "examples\$Example\main.cpp"
$mainCppBackup = $null
if ($InitialScene -ne "") {
    if (-not (Test-Path $mainCpp)) {
        throw "Cannot find $mainCpp to override initial scene"
    }
    $mainCppBackup = Get-Content -Raw -Path $mainCpp
    $patched = $mainCppBackup -replace 'sceneManager->loadScene\("[^"]+"\)', "sceneManager->loadScene(`"$InitialScene`")"
    if ($patched -eq $mainCppBackup) {
        Write-Warning "InitialScene patch did not match any loadScene(`"...`") call - launching with current main.cpp"
        $mainCppBackup = $null
    } else {
        Set-Content -Path $mainCpp -Value $patched -NoNewline
        Write-Host "Patched main.cpp to start with scene '$InitialScene'"
        # Need a rebuild to pick up the change
        & cmd /c "scripts\build-example.bat $Example $BuildType $Backend" *>&1 |
            Select-String -Pattern "SUCCESS|FAILED|error C" | Select-Object -First 4
    }
}

# P/Invoke helpers for window focus + bounds.
try {
    Add-Type @'
using System; using System.Runtime.InteropServices;
public class _VEWin {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
}
'@ -ErrorAction Stop
} catch {
    # already loaded in this session, ignore
}
Add-Type -AssemblyName System.Windows.Forms,System.Drawing

$outLog = Join-Path $exeDir "run_out.log"
$errLog = Join-Path $exeDir "run_err.log"
Remove-Item $outLog, $errLog -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath $exePath `
    -WorkingDirectory $exeDir `
    -PassThru `
    -RedirectStandardOutput $outLog `
    -RedirectStandardError  $errLog

Start-Sleep -Seconds $WaitSeconds
$proc.Refresh()

if ($proc.MainWindowHandle -eq [IntPtr]::Zero) {
    Write-Warning "Process has no main window yet"
} else {
    [_VEWin]::ShowWindow($proc.MainWindowHandle, 9) | Out-Null   # SW_RESTORE
    [_VEWin]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 600

    $rect = New-Object _VEWin+RECT
    if ([_VEWin]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)) {
        $w = $rect.Right  - $rect.Left
        $h = $rect.Bottom - $rect.Top
        if ($w -gt 0 -and $h -gt 0) {
            $bmp = New-Object System.Drawing.Bitmap $w, $h
            $gfx = [System.Drawing.Graphics]::FromImage($bmp)
            $gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
            $outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $repoRoot $Out }
            $bmp.Save($outPath)
            $gfx.Dispose(); $bmp.Dispose()
            Write-Host "Saved $outPath  (${w}x${h})"
        }
    }
}

if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}

$errBytes = (Get-Item $errLog -ErrorAction SilentlyContinue).Length
"STDERR bytes = $errBytes"
if ($errBytes -gt 0) { "  -> validation errors in $errLog" }

if ($null -ne $mainCppBackup) {
    Set-Content -Path $mainCpp -Value $mainCppBackup -NoNewline
    Write-Host "Reverted main.cpp"
}
