param([int]$ProcessId,[string]$OutputPath)
Add-Type -AssemblyName System.Drawing
if (-not ('ReceiverCapture' -as [type])) { Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ReceiverCapture {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd,out RECT rect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd,IntPtr hdc,uint flags);
}
"@
}
$window=(Get-Process -Id $ProcessId).MainWindowHandle
$rect=New-Object ReceiverCapture+RECT
if(-not [ReceiverCapture]::GetWindowRect($window,[ref]$rect)){throw 'Cannot read receiver window'}
$bitmap=New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$dc=$graphics.GetHdc()
try {if(-not [ReceiverCapture]::PrintWindow($window,$dc,2)){throw 'Cannot capture receiver window'}} finally {$graphics.ReleaseHdc($dc)}
try {$bitmap.Save($OutputPath,[Drawing.Imaging.ImageFormat]::Png)} finally {$graphics.Dispose();$bitmap.Dispose()}

