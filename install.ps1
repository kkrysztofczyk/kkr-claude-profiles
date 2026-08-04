# Instaluje claude-profiles.exe do %LOCALAPPDATA%\KKr\ClaudeProfiles
# i tworzy na pulpicie pojedynczy skrot "Claude".

$ErrorActionPreference = 'Stop'

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$source  = Join-Path $root 'build\claude-profiles.exe'
$destDir = Join-Path $env:LOCALAPPDATA 'KKr\ClaudeProfiles'
$dest    = Join-Path $destDir 'claude-profiles.exe'

if (-not (Test-Path $source)) {
    throw "Brak $source - uruchom najpierw build.cmd"
}

New-Item -ItemType Directory -Path $destDir -Force | Out-Null
Copy-Item $source $dest -Force
Write-Host "Zainstalowano: $dest"

$desktop = [Environment]::GetFolderPath('Desktop')
$shell   = New-Object -ComObject WScript.Shell

# Skroty z wczesniejszych wersji: osobny na kazde konto plus skrot do repozytorium.
foreach ($stale in 'Claude 1', 'Claude 2', 'ClaudeLauncher (projekt)') {
    $path = Join-Path $desktop "$stale.lnk"
    if (Test-Path $path) {
        Remove-Item $path -Force
        Write-Host "Usunieto stary skrot: $stale"
    }
}

# Jeden skrot bez argumentow - launcher sam pyta o konto.
# Ikona pochodzi z zasobow .exe, wiec nie trzeba osobnego pliku .ico.
$path = Join-Path $desktop 'Claude.lnk'
$lnk = $shell.CreateShortcut($path)
$lnk.TargetPath       = $dest
$lnk.WorkingDirectory = $destDir
$lnk.IconLocation     = "$dest,0"
$lnk.Description      = 'Claude Desktop - wybor konta'
$lnk.Save()
Write-Host "Skrot: $path"

# Sprzatanie po poprzednich lokalizacjach instalacji. Bez tego zostawalyby dwie
# kopie programu, a skrot moglby wskazywac na te starsza.
#
# Kazde niepowodzenie jest tu tylko ostrzezeniem: katalog bywa zablokowany przez
# dzialajacy proces, a instalacja jest w tym momencie juz zakonczona - przerywanie
# jej z powodu nieudanego sprzatania byloby myleniem uzytkownika.
foreach ($legacyDir in (Join-Path $env:LOCALAPPDATA 'ClaudeLauncher'),
                       (Join-Path $env:LOCALAPPDATA 'ClaudeKonto2')) {
    if (Test-Path $legacyDir) {
        try {
            Remove-Item $legacyDir -Recurse -Force -ErrorAction Stop
            Write-Host "Usunieto poprzednia instalacje: $legacyDir"
        } catch {
            Write-Warning "Nie udalo sie usunac $legacyDir - usun go recznie. ($($_.Exception.Message))"
        }
    }
}
