# Usuwa program i skroty z pulpitu. Profile kont (Claude-konto*) zostaja
# nietkniete - siedzi w nich zalogowana sesja.

$ErrorActionPreference = 'Stop'

$destDir = Join-Path $env:LOCALAPPDATA 'KKr\ClaudeProfiles'
$desktop = [Environment]::GetFolderPath('Desktop')

foreach ($name in 'Claude', 'Claude 1', 'Claude 2', 'ClaudeLauncher (projekt)') {
    $path = Join-Path $desktop "$name.lnk"
    if (Test-Path $path) {
        Remove-Item $path -Force
        Write-Host "Usunieto skrot: $path"
    }
}

# Biezaca lokalizacja plus poprzednie, zeby odinstalowanie sprzatalo po kazdej wersji.
# Zablokowany katalog nie przerywa reszty - lepiej usunac ile sie da i powiedziec
# o reszcie, niz zostawic uzytkownika w polowie odinstalowanego stanu.
foreach ($dir in $destDir,
                 (Join-Path $env:LOCALAPPDATA 'ClaudeLauncher'),
                 (Join-Path $env:LOCALAPPDATA 'ClaudeKonto2')) {
    if (Test-Path $dir) {
        try {
            Remove-Item $dir -Recurse -Force -ErrorAction Stop
            Write-Host "Usunieto: $dir"
        } catch {
            Write-Warning "Nie udalo sie usunac $dir - usun go recznie. ($($_.Exception.Message))"
        }
    }
}

# Katalog KKr zostaje, jesli mieszkaja w nim inne narzedzia.
$kkrDir = Join-Path $env:LOCALAPPDATA 'KKr'
if ((Test-Path $kkrDir) -and -not (Get-ChildItem $kkrDir -Force)) {
    Remove-Item $kkrDir -Force
}

Write-Host ""
Write-Host "Profile kont pozostaly nietkniete. Szukaj ich w:"
foreach ($root in $env:APPDATA,
                  (Join-Path $env:LOCALAPPDATA 'Packages\Claude_pzs8sxrjxfjjc\LocalCache\Roaming')) {
    if (Test-Path $root) {
        Get-ChildItem $root -Directory -Filter 'Claude-konto*' -ErrorAction SilentlyContinue |
            ForEach-Object { Write-Host "  $($_.FullName)" }
    }
}
