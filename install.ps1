# FastShell installer for Windows: current user only, no administrator rights.
#
#   Install or update:  irm https://raw.githubusercontent.com/cpu-alt/FastShell/main/install.ps1 | iex
#   Uninstall:          & "$env:LOCALAPPDATA\Programs\FastShell\install.ps1" -Uninstall
#
# It downloads the latest release from GitHub, checks its SHA-256 against
# the published one, installs it in %LOCALAPPDATA%\Programs\FastShell and
# adds Start menu and desktop shortcuts. Sessions and settings are kept.
param(
    [switch]$Uninstall,
    [string]$Version = $env:FASTSHELL_VERSION,  # e.g. 1.2.0; empty = latest release
    [string]$InstallDir = $(if ($env:FASTSHELL_DIR) { $env:FASTSHELL_DIR } else { Join-Path $env:LOCALAPPDATA 'Programs\FastShell' }),
    [string]$Package,                            # a local .zip instead of downloading (its .sha256 next to it)
    [switch]$NoDesktopShortcut,
    [switch]$FilesOnly,                          # portable copy: no shortcuts, no entry in Windows settings
    [switch]$NoStart
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # much faster downloads on Windows PowerShell 5.1
$Repo = 'cpu-alt/FastShell'
$UninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\FastShell'
$StartMenuLink = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\FastShell.lnk'
$DesktopLink = Join-Path ([Environment]::GetFolderPath('Desktop')) 'FastShell.lnk'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Write-Step($text) { Write-Host "  $text" -ForegroundColor Cyan }

function Stop-FastShell([string]$dir) {
    $full = [IO.Path]::GetFullPath($dir).TrimEnd('\') + '\'
    Get-Process FastShell -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($full, [StringComparison]::OrdinalIgnoreCase) } |
        ForEach-Object {
            Write-Step "Closing FastShell (process $($_.Id))"
            $null = $_.CloseMainWindow()
            if (-not $_.WaitForExit(5000)) { Stop-Process -Id $_.Id -Force; Start-Sleep -Milliseconds 500 }
        }
}

function New-Shortcut([string]$path, [string]$target) {
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($path)
    $shortcut.TargetPath = $target
    $shortcut.WorkingDirectory = Split-Path $target
    $shortcut.Description = 'FastShell terminal'
    $shortcut.Save()
}

function Get-ExpectedHash([string]$file) {
    $hash = ((Get-Content -LiteralPath $file -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
    if ($hash -notmatch '^[0-9a-f]{64}$') { throw "The checksum file is not a SHA-256: $file" }
    return $hash
}

function Assert-ReleaseUrl([string]$url) {
    if (-not $url.StartsWith("https://github.com/$Repo/releases/download/", [StringComparison]::OrdinalIgnoreCase) -or $url.Contains('..')) {
        throw "Refusing a download that is not a FastShell release: $url"
    }
}

if ($Uninstall) {
    Write-Host 'Uninstalling FastShell...' -ForegroundColor White
    Stop-FastShell $InstallDir
    foreach ($link in $StartMenuLink, $DesktopLink) { if (Test-Path -LiteralPath $link) { Remove-Item -LiteralPath $link -Force } }
    if (Test-Path -LiteralPath (Join-Path $InstallDir 'FastShell.exe')) { Remove-Item -LiteralPath $InstallDir -Recurse -Force }
    if (Test-Path $UninstallKey) { Remove-Item $UninstallKey -Recurse -Force }
    Write-Host 'FastShell is uninstalled. Your sessions are kept in %APPDATA%\EMF NET\FastShell.' -ForegroundColor Green
    return
}

Write-Host 'Installing FastShell...' -ForegroundColor White
$work = Join-Path ([IO.Path]::GetTempPath()) ('FastShell-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    if ($Package) {
        $zip = (Resolve-Path -LiteralPath $Package).Path
        $sha = "$zip.sha256"
        $label = Split-Path $zip -Leaf
    } else {
        $api = if ($Version) { "https://api.github.com/repos/$Repo/releases/tags/v$($Version.TrimStart('v'))" }
               else { "https://api.github.com/repos/$Repo/releases/latest" }
        Write-Step 'Looking for the latest release'
        try {
            $release = Invoke-RestMethod -Uri $api -Headers @{ 'User-Agent' = 'FastShell-installer'; 'Accept' = 'application/vnd.github+json' }
        } catch {
            if ($_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq 404) {
                throw "No FastShell release$(if ($Version) { " $Version" }) is published on https://github.com/$Repo/releases yet."
            }
            throw "Cannot reach GitHub: $($_.Exception.Message)"
        }
        $zipAsset = $release.assets | Where-Object { $_.name -like 'FastShell-*-windows-x64.zip' } | Select-Object -First 1
        if (-not $zipAsset) { throw "Release $($release.tag_name) has no Windows package." }
        $shaAsset = $release.assets | Where-Object { $_.name -eq "$($zipAsset.name).sha256" } | Select-Object -First 1
        if (-not $shaAsset) { throw "Release $($release.tag_name) has no checksum for $($zipAsset.name)." }
        Assert-ReleaseUrl $zipAsset.browser_download_url
        Assert-ReleaseUrl $shaAsset.browser_download_url
        $zip = Join-Path $work $zipAsset.name
        $sha = "$zip.sha256"
        $label = "$($release.tag_name) ($([math]::Round($zipAsset.size / 1MB)) MB)"
        Write-Step "Downloading FastShell $label"
        Invoke-WebRequest -Uri $shaAsset.browser_download_url -OutFile $sha -UseBasicParsing -Headers @{ 'User-Agent' = 'FastShell-installer' }
        Invoke-WebRequest -Uri $zipAsset.browser_download_url -OutFile $zip -UseBasicParsing -Headers @{ 'User-Agent' = 'FastShell-installer' }
    }

    Write-Step 'Checking the SHA-256 checksum'
    $expected = Get-ExpectedHash $sha
    $actual = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "Checksum mismatch: the download is damaged or was modified. Nothing was installed." }

    Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $work 'unpacked') -Force
    $source = Join-Path $work 'unpacked\FastShell'
    if (-not (Test-Path -LiteralPath (Join-Path $source 'FastShell.exe'))) { throw 'The package has no FastShell.exe.' }

    Stop-FastShell $InstallDir
    Write-Step "Installing in $InstallDir"
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $InstallDir -Recurse -Force
    $exe = Join-Path $InstallDir 'FastShell.exe'

    $installedVersion = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
    if ($FilesOnly) {
        Write-Host "FastShell $installedVersion is copied to $InstallDir." -ForegroundColor Green
        return
    }

    New-Shortcut $StartMenuLink $exe
    if (-not $NoDesktopShortcut) { New-Shortcut $DesktopLink $exe }

    New-Item -Path $UninstallKey -Force | Out-Null
    $values = @{
        DisplayName = 'FastShell'; DisplayVersion = $installedVersion; Publisher = 'Nathan Witz & Patrizio Ventura'
        InstallLocation = $InstallDir; DisplayIcon = $exe
        UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $InstallDir 'install.ps1')`" -Uninstall"
        URLInfoAbout = "https://github.com/$Repo"
    }
    foreach ($entry in $values.GetEnumerator()) { Set-ItemProperty -Path $UninstallKey -Name $entry.Key -Value $entry.Value }
    Set-ItemProperty -Path $UninstallKey -Name NoModify -Value 1 -Type DWord
    Set-ItemProperty -Path $UninstallKey -Name NoRepair -Value 1 -Type DWord

    Write-Host "FastShell $installedVersion is installed. Start it from the Start menu or the desktop." -ForegroundColor Green
    if (-not $NoStart) { Start-Process -FilePath $exe }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
