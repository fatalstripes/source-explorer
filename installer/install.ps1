<#
.SYNOPSIS
  Installs (or removes) the Source Explorer plugin for OBS Studio on Windows 64-bit.

.DESCRIPTION
  Copies source-explorer.dll to  %ProgramData%\obs-studio\plugins\source-explorer\bin\64bit\ ,
  the folder OBS Studio 32.x scans for third-party plugins.

  The plugin is built against one specific OBS and Qt, and loading it into a different one can
  crash OBS on start-up, so the installer refuses anything but OBS 32.2.x with Qt 6.11.x.

.PARAMETER Uninstall      Remove the plugin instead of installing it.
.PARAMETER Quiet          Never wait for a key press (for scripted use).
.PARAMETER CheckOnly      Run every check and report, but change nothing.
.PARAMETER ObsDir         OBS install folder, if it is not in the usual place.
.PARAMETER IgnoreVersion  Install even if the OBS/Qt version is not the supported one. May crash OBS.

Testing switches (not for normal use): -PluginRoot, -AssumeObsVersion, -IgnoreRunningCheck.
#>
[CmdletBinding()]
param(
	[switch]$Uninstall,
	[switch]$Quiet,
	[switch]$CheckOnly,
	[string]$ObsDir,
	[switch]$IgnoreVersion,
	[string]$PluginRoot,
	[string]$AssumeObsVersion,
	[switch]$IgnoreRunningCheck,
	[switch]$Elevated
)

$ErrorActionPreference = "Stop"

$PluginName = "source-explorer"
$SupportedObs = "32.2."   # the OBS this build was made for (32.2.2)
$SupportedQt = "6.11."    # OBS bundles its own Qt; a different one makes the plugin unsafe to load

function Wait-Key {
	if (-not $Quiet) {
		Write-Host ""
		Write-Host "Press any key to close this window..." -ForegroundColor DarkGray
		$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
	}
}

function Stop-WithError([string]$message) {
	Write-Host ""
	Write-Host "Nothing was changed." -ForegroundColor Yellow
	Write-Host "ERROR: $message" -ForegroundColor Red
	Wait-Key
	exit 1
}

function Get-FileProductVersion([string]$path) {
	if (Test-Path $path) { return (Get-Item $path).VersionInfo.ProductVersion } else { return $null }
}

Write-Host ""
Write-Host "Source Explorer for OBS Studio" -ForegroundColor Cyan
Write-Host "------------------------------"

# --- 1. platform ----------------------------------------------------------------------
if (-not [Environment]::Is64BitOperatingSystem) {
	Stop-WithError "This plugin is for 64-bit Windows only."
}

# --- 2. where is OBS? -------------------------------------------------------------------
function Find-ObsDir {
	if ($ObsDir) { return $ObsDir }
	foreach ($key in "HKLM:\SOFTWARE\OBS Studio", "HKLM:\SOFTWARE\WOW6432Node\OBS Studio") {
		if (Test-Path $key) {
			$dir = (Get-ItemProperty $key).'(default)'
			if ($dir -and (Test-Path (Join-Path $dir "bin\64bit\obs64.exe"))) { return $dir }
		}
	}
	$default = Join-Path $env:ProgramFiles "obs-studio"
	if (Test-Path (Join-Path $default "bin\64bit\obs64.exe")) { return $default }
	return $null
}

$obs = Find-ObsDir
if (-not $obs -or -not (Test-Path (Join-Path $obs "bin\64bit\obs64.exe"))) {
	Stop-WithError ("OBS Studio was not found. Install the regular Windows version of OBS Studio first. " +
		"(The Steam and portable versions are not supported by this installer.)")
}
$obsExe = Join-Path $obs "bin\64bit\obs64.exe"
$obsVersion = if ($AssumeObsVersion) { $AssumeObsVersion } else { Get-FileProductVersion $obsExe }
$qtVersion = Get-FileProductVersion (Join-Path $obs "bin\64bit\Qt6Core.dll")
Write-Host "OBS Studio : $obsVersion  ($obs)"
Write-Host "Qt (in OBS): $qtVersion"

# --- 3. is this the OBS the plugin was built for? ---------------------------------------------
if (-not $Uninstall) {
	$obsOk = $obsVersion -and $obsVersion.StartsWith($SupportedObs)
	$qtOk = $qtVersion -and $qtVersion.StartsWith($SupportedQt)
	if (-not ($obsOk -and $qtOk)) {
		if ($IgnoreVersion) {
			Write-Host "WARNING: unsupported OBS/Qt version, installing anyway because of -IgnoreVersion." -ForegroundColor Yellow
		} else {
			Stop-WithError ("This build is made for OBS Studio ${SupportedObs}x (Qt ${SupportedQt}x). " +
				"You have OBS $obsVersion with Qt $qtVersion, and loading it could crash OBS. " +
				"Ask the author for a build that matches your OBS version.")
		}
	}
}

# --- 4. OBS must be closed ----------------------------------------------------------------
if (-not $IgnoreRunningCheck -and (Get-Process obs64 -ErrorAction SilentlyContinue)) {
	Stop-WithError ("OBS Studio is running. Close it completely (File > Exit; also check the system tray " +
		"near the clock) and run this again.")
}

# --- 5. the plugin file -----------------------------------------------------------------
$source = Join-Path $PSScriptRoot "$PluginName.dll"
if (-not $Uninstall -and -not (Test-Path $source)) {
	Stop-WithError "$PluginName.dll is missing from the installer folder ($PSScriptRoot)."
}

$root = if ($PluginRoot) { $PluginRoot } else { Join-Path $env:ProgramData "obs-studio\plugins" }
$pluginDir = Join-Path $root $PluginName
$destDir = Join-Path $pluginDir "bin\64bit"
$destFile = Join-Path $destDir "$PluginName.dll"

if ($CheckOnly) {
	Write-Host ""
	Write-Host "All checks passed. Would $(if ($Uninstall) { 'remove' } else { 'install to' }): $pluginDir" -ForegroundColor Green
	Wait-Key
	exit 0
}

# --- 6. do it (ask for admin rights only if Windows says no) --------------------------------------
function Test-IsAdmin {
	$id = [Security.Principal.WindowsIdentity]::GetCurrent()
	return (New-Object Security.Principal.WindowsPrincipal $id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

try {
	if ($Uninstall) {
		if (Test-Path $pluginDir) {
			Remove-Item $pluginDir -Recurse -Force
			Write-Host ""
			Write-Host "Removed $pluginDir" -ForegroundColor Green
		} else {
			Write-Host ""
			Write-Host "Source Explorer is not installed (nothing at $pluginDir)." -ForegroundColor Yellow
		}
		Wait-Key
		exit 0
	}

	$updating = Test-Path $destFile
	New-Item -ItemType Directory -Force -Path $destDir | Out-Null
	Copy-Item -LiteralPath $source -Destination $destFile -Force
	# The plugin's (tiny) English locale file; without it OBS logs a harmless "failed to load text" line.
	$locale = Join-Path $PSScriptRoot "en-US.ini"
	if (Test-Path $locale) {
		$localeDir = Join-Path $pluginDir "data\locale"
		New-Item -ItemType Directory -Force -Path $localeDir | Out-Null
		Copy-Item -LiteralPath $locale -Destination (Join-Path $localeDir "en-US.ini") -Force
	}
} catch [System.UnauthorizedAccessException] {
	if (-not $Elevated -and -not (Test-IsAdmin)) {
		Write-Host "Windows needs permission to write there. Asking for administrator rights..." -ForegroundColor Yellow
		$argList = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Elevated")
		if ($Uninstall) { $argList += "-Uninstall" }
		if ($Quiet) { $argList += "-Quiet" }
		if ($ObsDir) { $argList += @("-ObsDir", "`"$ObsDir`"") }
		if ($IgnoreVersion) { $argList += "-IgnoreVersion" }
		if ($PluginRoot) { $argList += @("-PluginRoot", "`"$PluginRoot`"") }
		$p = Start-Process powershell -Verb RunAs -ArgumentList $argList -Wait -PassThru
		exit $p.ExitCode
	}
	Stop-WithError "Windows would not let this installer write to $destDir, even with administrator rights."
} catch {
	Stop-WithError "Could not install: $($_.Exception.Message)"
}

# --- 7. check it landed intact ---------------------------------------------------------------
$want = (Get-FileHash $source -Algorithm SHA256).Hash
$got = (Get-FileHash $destFile -Algorithm SHA256).Hash
if ($want -ne $got) {
	Stop-WithError "The copied file does not match the original. Try again, and check your antivirus is not interfering."
}

Write-Host ""
Write-Host $(if ($updating) { "Updated." } else { "Installed." }) -ForegroundColor Green
Write-Host "  $destFile"
Write-Host "  SHA-256 $got"
Write-Host ""
Write-Host "Next:" -ForegroundColor Cyan
Write-Host "  1. Start OBS Studio."
Write-Host "  2. Open the  Docks  menu and tick  Source Explorer."
Write-Host "  3. If you get a crash prompt at start-up, choose 'Run in Normal Mode'."
Write-Host ""
Write-Host "To remove it later, delete this folder:  $pluginDir" -ForegroundColor DarkGray
Wait-Key
exit 0
