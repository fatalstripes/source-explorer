<#
.SYNOPSIS
  Packages the built plugin into  dist\SourceExplorer-Setup.exe  (single self-extracting file,
  made with Windows' built-in IExpress) and  dist\SourceExplorer-Setup.zip  (the same files).

.EXAMPLE
  cmake --build build --config Release
  ./installer/build-installer.ps1
#>
[CmdletBinding()]
param(
	[string]$Dll = (Join-Path $PSScriptRoot "..\build\Release\source-explorer.dll")
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist"

if (-not (Test-Path $Dll)) { throw "Plugin DLL not found: $Dll  (build it first)" }

# IExpress cannot handle a space anywhere in the paths in its script (and this project lives in
# "Source Explorer"), so it is run from a space-free temporary folder and the results are copied back.
$tempBase = if ($env:TEMP -match ' ') { $env:SystemDrive + "\" } else { $env:TEMP }
$work = Join-Path $tempBase ("se-pack-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
$stage = Join-Path $work "stage"
$workExe = Join-Path $work "SourceExplorer-Setup.exe"

try {
	New-Item -ItemType Directory -Force $stage | Out-Null
	Copy-Item $Dll (Join-Path $stage "source-explorer.dll")
	foreach ($f in "install.ps1", "Install.cmd", "Uninstall.cmd", "README.txt") {
		Copy-Item (Join-Path $PSScriptRoot $f) (Join-Path $stage $f)
	}
	Copy-Item (Join-Path $root "data\locale\en-US.ini") (Join-Path $stage "en-US.ini")
	Copy-Item (Join-Path $root "data\manifest.json") (Join-Path $stage "manifest.json")

	# ---- the EXE: IExpress extracts to a temp folder and runs Install.cmd ---------------------------
	# (The uninstaller is only in the zip: an EXE that extracts to a temp folder cannot keep files.)
	$files = "source-explorer.dll", "install.ps1", "Install.cmd", "README.txt", "en-US.ini", "manifest.json"
	$fileDefs = ($files | ForEach-Object -Begin { $i = 0 } -Process { "FILE$i=`"$_`""; $i++ }) -join "`r`n"
	$fileRefs = ($files | ForEach-Object -Begin { $i = 0 } -Process { "%FILE$i%="; $i++ }) -join "`r`n"
	$sed = Join-Path $work "package.sed"
	@"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=Install Source Explorer for OBS Studio? Close OBS first.
DisplayLicense=
FinishMessage=
TargetName=$workExe
FriendlyName=Source Explorer for OBS Studio
AppLaunched=cmd /c Install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
$fileDefs
[SourceFiles]
SourceFiles0=$stage\
[SourceFiles0]
$fileRefs
"@ | Set-Content -Path $sed -Encoding ASCII

	$p = Start-Process "$env:WINDIR\System32\iexpress.exe" -ArgumentList "/N", "/Q", $sed -Wait -PassThru
	if ($p.ExitCode -ne 0 -or -not (Test-Path $workExe)) { throw "IExpress failed (exit code $($p.ExitCode))" }

	# ---- publish: fresh dist\ with just the two deliverables ------------------------------------------
	if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
	New-Item -ItemType Directory -Force $dist | Out-Null
	Copy-Item $workExe (Join-Path $dist "SourceExplorer-Setup.exe")
	Compress-Archive -Path (Join-Path $stage "*") -DestinationPath (Join-Path $dist "SourceExplorer-Setup.zip") -CompressionLevel Optimal
} finally {
	if (Test-Path $work) { Remove-Item $work -Recurse -Force }
}

Write-Host ""
foreach ($name in "SourceExplorer-Setup.exe", "SourceExplorer-Setup.zip") {
	$f = Join-Path $dist $name
	"{0,-28} {1,10:N0} bytes   SHA-256 {2}" -f $name, (Get-Item $f).Length, (Get-FileHash $f -Algorithm SHA256).Hash
}
