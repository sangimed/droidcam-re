param([string]$ImagePath)
Add-Type -AssemblyName System.Drawing
$source=[Drawing.Image]::FromFile($ImagePath)
$crop=New-Object Drawing.Bitmap ($source.Width*2),160
$graphics=[Drawing.Graphics]::FromImage($crop)
$graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$graphics.DrawImage($source,(New-Object Drawing.Rectangle 0,0,($source.Width*2),160),(New-Object Drawing.Rectangle 0,35,$source.Width,80),[Drawing.GraphicsUnit]::Pixel)
$cropPath=$ImagePath+'.overlay.png'
$crop.Save($cropPath,[Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose();$crop.Dispose();$source.Dispose()
$ImagePath=$cropPath
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null=[Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime]
$null=[Windows.Media.Ocr.OcrEngine,Windows.Foundation,ContentType=WindowsRuntime]
$null=[Windows.Graphics.Imaging.BitmapDecoder,Windows.Graphics.Imaging,ContentType=WindowsRuntime]
$asTask=[System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {$_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetGenericArguments().Count -eq 1 -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'} | Select-Object -First 1
function Await($operation,[type]$resultType) { $task=$asTask.MakeGenericMethod($resultType).Invoke($null,@($operation)); $task.Wait(); $task.Result }
$file=Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ImagePath)) ([Windows.Storage.StorageFile])
$stream=Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$decoder=Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$bitmap=Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$engine=[Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
$result=Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
$result.Lines | ForEach-Object {$_.Text}
$bitmap.Dispose();$stream.Dispose()
