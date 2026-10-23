# Starts the unmodified client windowed in its asset directory, waits, then saves
# a desktop screenshot and a capture of the client window. Spike-only: the real
# rig A uses the client's own capture mode instead of screen grabs.
param(
	[Parameter(Mandatory)] [string] $ClientDir,
	[Parameter(Mandatory)] [string] $Exe,
	[Parameter(Mandatory)] [string] $OutDir
)

$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$ClientDir = (Resolve-Path $ClientDir).Path

Copy-Item $Exe $ClientDir -Force
@"
[Screen]
WindowMode=1
VSyncEnabled=0
[ViewPort]
Width=1024
Height=768
ColorDepth=32
[Sound]
Bgm=0
Effect=0
"@ | Set-Content -Encoding ascii (Join-Path $ClientDir 'Option.ini')

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

function Save-Desktop([string] $path) {
	$b = [System.Windows.Forms.SystemInformation]::VirtualScreen
	$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
	$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$g.Dispose(); $bmp.Dispose()
}

function Save-Window([IntPtr] $hwnd, [string] $path) {
	$r = New-Object Win+RECT
	[Win]::GetWindowRect($hwnd, [ref] $r) | Out-Null
	$w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
	if ($w -le 0 -or $h -le 0) { return }
	$bmp = New-Object System.Drawing.Bitmap $w, $h
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$hdc = $g.GetHdc()
	# PW_RENDERFULLCONTENT (2) so DirectX content is included
	[Win]::PrintWindow($hwnd, $hdc, 2) | Out-Null
	$g.ReleaseHdc($hdc)
	$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$g.Dispose(); $bmp.Dispose()
}

$proc = Start-Process -FilePath (Join-Path $ClientDir 'KnightOnLine.exe') -WorkingDirectory $ClientDir -PassThru
$elapsed = 0
foreach ($t in 10, 25, 45) {
	Start-Sleep -Seconds ($t - $elapsed)
	$elapsed = $t
	$proc.Refresh()
	"t=${t}s exited=$($proc.HasExited) window='$($proc.MainWindowTitle)' handle=$($proc.MainWindowHandle)" | Tee-Object -Append (Join-Path $OutDir 'client-state.txt')
	Save-Desktop (Join-Path $OutDir "desktop-${t}s.png")
	if (-not $proc.HasExited -and $proc.MainWindowHandle -ne 0) {
		Save-Window $proc.MainWindowHandle (Join-Path $OutDir "window-${t}s.png")
	}
}

if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
else { "exit code: $($proc.ExitCode)" | Tee-Object -Append (Join-Path $OutDir 'client-state.txt') }

# Anything the client logged during the run
Get-ChildItem $ClientDir -File | Where-Object { $_.LastWriteTime -gt $proc.StartTime -and $_.Extension -in '.log', '.txt', '.ini' } |
	Copy-Item -Destination $OutDir -Force
