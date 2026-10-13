<#
.SYNOPSIS
  Prepares the local build SDK in ./sdk and configures ./build (Windows x64).

.DESCRIPTION
  Downloads three pinned archives, verifies SHA-256 where OBS publishes a hash,
  extracts them each into their own folder, generates import libraries from your
  installed OBS, and runs the CMake configure step.

    OBS source release   headers only (libobs, frontend API)
    obs-deps             general prebuilt dependencies (hash pinned by OBS)
    obs-deps Qt6         Qt 6 headers/libs/moc (hash pinned by OBS)

  Safe to re-run: steps whose output already exists are skipped.
  The pins below must match your installed OBS (check Help > About).

.EXAMPLE
  ./cmake/setup-sdk.ps1
  cmake --build build --config Release --target source-explorer
#>
param(
	[string]$ObsDir = "C:\Program Files\obs-studio",
	[string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# --- pins (from obs-studio 32.2.2 CMakePresets.json) ---------------------------------
$ObsVersion = "32.2.2"
$DepsVersion = "2026-07-15"
$Pins = @(
	@{ Name = "src";  Dir = "obs-src"; Archive = "obs-sources.tar.gz"
	   Url = "https://github.com/obsproject/obs-studio/releases/download/$ObsVersion/OBS-Studio-$ObsVersion-Sources.tar.gz"
	   Sha256 = $null; Check = "obs-studio-$ObsVersion-sources\libobs\obs.h" },
	@{ Name = "deps"; Dir = "deps"; Archive = "deps.zip"
	   Url = "https://github.com/obsproject/obs-deps/releases/download/$DepsVersion/windows-deps-$DepsVersion-x64.zip"
	   Sha256 = "6f90e9598fa10cff5ad23cdcfae49b87868c07bf896b02cd464582b4ce2f2ba9"; Check = "include" },
	@{ Name = "qt";   Dir = "qt6"; Archive = "qt6.zip"
	   Url = "https://github.com/obsproject/obs-deps/releases/download/$DepsVersion/windows-deps-qt6-$DepsVersion-x64.zip"
	   Sha256 = "7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579"; Check = "lib\cmake\Qt6\Qt6Config.cmake" }
)

$root = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $root "sdk"

# Installed OBS must match the headers we are about to use.
$exe = Join-Path $ObsDir "bin\64bit\obs64.exe"
if (Test-Path $exe) {
	$installed = (Get-Item $exe).VersionInfo.ProductVersion
	if ($installed -ne $ObsVersion) {
		Write-Warning "Installed OBS is $installed but this script pins $ObsVersion. Update the pins first."
	}
}

foreach ($p in $Pins) {
	$target = Join-Path $sdk $p.Dir
	if (Test-Path (Join-Path $target $p.Check)) { "[skip] $($p.Name): already extracted"; continue }

	# Each download and extraction gets its own new directory; archives are untrusted data.
	$dl = Join-Path $sdk "dl-$($p.Name)"
	New-Item -ItemType Directory -Force $dl, $target | Out-Null
	$file = Join-Path $dl $p.Archive
	if (-not (Test-Path $file)) {
		"[get ] $($p.Name): $($p.Url)"
		Invoke-WebRequest $p.Url -OutFile $file
	}
	if ($p.Sha256) {
		$actual = (Get-FileHash $file -Algorithm SHA256).Hash.ToLower()
		if ($actual -ne $p.Sha256) { throw "SHA-256 mismatch for $($p.Name): got $actual" }
		"[ok  ] $($p.Name): sha256 verified"
	}
	# bsdtar refuses path traversal. Symlinks to formatter scripts fail on Windows; harmless.
	tar -xf $file -C $target 2>$null
	if (-not (Test-Path (Join-Path $target $p.Check))) { throw "Extraction of $($p.Name) did not produce $($p.Check)" }
}

$implibs = Join-Path $sdk "implibs"
if (-not (Test-Path (Join-Path $implibs "obs.lib"))) {
	& (Join-Path $PSScriptRoot "make-import-libs.ps1") -ObsDir $ObsDir -OutDir $implibs
} else {
	"[skip] import libraries already generated"
}

# Prefer a working cmake on PATH; otherwise use the one bundled with Visual Studio.
$cmake = "cmake"
& cmake --version *> $null
if ($LASTEXITCODE -ne 0 -or -not $?) {
	$bundled = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
	if (-not $bundled) { throw "No working cmake found. Install CMake or the VS 'C++ CMake tools' component." }
	$cmake = $bundled.FullName
	"[note] using $cmake"
}

& $cmake -S $root -B (Join-Path $root "build") -G $Generator -A x64 "-DSE_OBS_SDK_DIR=$sdk"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
"Done. Build with: cmake --build build --config Release --target source-explorer"
