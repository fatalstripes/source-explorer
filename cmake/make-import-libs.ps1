<#
.SYNOPSIS
  Generates MSVC import libraries (.lib) from an installed OBS Studio's DLLs.

.DESCRIPTION
  Linking against the DLLs you actually run guarantees the plugin matches that
  OBS build's exports. Run from any shell; it locates the MSVC tools itself.

.EXAMPLE
  ./cmake/make-import-libs.ps1 -ObsDir "C:\Program Files\obs-studio" -OutDir sdk\implibs
#>
param(
	[string]$ObsDir = "C:\Program Files\obs-studio",
	[Parameter(Mandatory)][string]$OutDir,
	[string[]]$Dlls = @("obs", "obs-frontend-api")
)

$ErrorActionPreference = "Stop"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
	$vsRoot = & $vswhere -latest -products * -property installationPath
} else {
	$vsRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community"
}
$vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsRoot" }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$bin = Join-Path $ObsDir "bin\64bit"

foreach ($name in $Dlls) {
	$dll = Join-Path $bin "$name.dll"
	if (-not (Test-Path $dll)) { throw "Missing $dll" }

	$dump = cmd /c "`"$vcvars`" >nul 2>&1 && dumpbin /exports `"$dll`""
	# Export table rows look like: "       1    0 0001A2B0 obs_startup"
	$names = foreach ($line in $dump) {
		if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $Matches[1] }
	}
	$names = $names | Where-Object { $_ -and $_ -notmatch '^\[NONAME\]' } | Sort-Object -Unique
	if (-not $names) { throw "No exports found in $dll" }

	$def = Join-Path $OutDir "$name.def"
	$lib = Join-Path $OutDir "$name.lib"
	@("LIBRARY $name", "EXPORTS") + $names | Set-Content -Encoding ascii $def
	cmd /c "`"$vcvars`" >nul 2>&1 && lib /nologo /machine:x64 /def:`"$def`" /out:`"$lib`"" | Out-Null
	if (-not (Test-Path $lib)) { throw "Failed to create $lib" }
	"{0}: {1} exports -> {2}" -f $name, @($names).Count, $lib
}
