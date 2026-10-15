# partita.ps1 - fa girare il port senza finestra con ingressi finti e salva
# l'ultimo quadro come PNG; con -traccia elenca anche le frasi dette al
# narrator, con -verifica fa la prova contro Musashi.
#   tools\partita.ps1 -quadri 3000 -ingressi "p320.256:0,m:150" [-png uscita.png]
#                     [-foto "1000-2000/100,2500"] [-traccia] [-wav voce.wav] [-verifica 5] [-exe build\cmake\TotalChaosAGA711Recomp.exe]
# Ingressi (AMIGA_INGRESSI): p<x>.<y>:<quadro> sposta il mouse (video 640x512),
# m:<quadro> clic sinistro (5 quadri), k<hex>:<quadro> tasto (codice Amiga).
param([int]$quadri = 1500, [string]$ingressi = "", [string]$png = "",
      [switch]$traccia, [string]$wav = "", [int]$verifica = 0,
      [string]$exe = "build\cmake\TotalChaosAGA711Recomp.exe", [long]$ora = 1792000000, [int]$tracciada = 0,
      [string]$foto = "", [int]$limite = 600)
$repo = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($exe)) { $exe = Join-Path $repo $exe }
$b = Split-Path $exe -Parent
$env:AMIGA_QUADRI = "$quadri"
$env:AMIGA_INGRESSI = $ingressi
$env:AMIGA_TRACCIA_OS = $(if ($traccia) { "1" } else { $null })
$env:AMIGA_WAV = $(if ($wav) { $wav } else { $null })
$env:AMIGA_VERIFICA = $(if ($verifica) { "$verifica" } else { $null })
$env:AMIGA_ORA = "$ora"                      # ora fissa: stesso seme, partita ripetibile
$env:AMIGA_TRACCIA_DA = $(if ($tracciada) { "$tracciada" } else { $null })
$env:AMIGA_FOTO = $(if ($foto) { $foto } else { $null })   # foto_<quadro>.bmp accanto all'eseguibile
# una copia dei dati: il gioco riscrive HighScores, Time e gli elenchi, e con
# l'ora fissa le prove lasciavano record datati 14/10/2026 nei dati veri
$tolgo_dati = $false
if (-not $env:TC_DATI) {
    $dati = Join-Path $repo "build\dati_prova"
    python -I -c "import sys; sys.path.insert(0, sys.argv[1]); import prove_cpu; prove_cpu.copia_dati(sys.argv[2])" $PSScriptRoot $dati
    $env:TC_DATI = $dati
    $tolgo_dati = $true
}
$p =Start-Process $exe -WorkingDirectory $b -NoNewWindow -PassThru `
     -RedirectStandardOutput (Join-Path $b "out.txt") -RedirectStandardError (Join-Path $b "err.txt")
if (-not $p.WaitForExit($limite * 1000)) { $p.Kill(); "TIMEOUT dopo $limite s" }
foreach ($v in "AMIGA_QUADRI", "AMIGA_INGRESSI", "AMIGA_TRACCIA_OS", "AMIGA_WAV", "AMIGA_VERIFICA", "AMIGA_ORA", "AMIGA_TRACCIA_DA", "AMIGA_FOTO") {
    Set-Item -Path "Env:$v" -Value $null
}
if ($tolgo_dati) { $env:TC_DATI = $null }
$err = Get-Content (Join-Path $b "err.txt")
$err | Select-String -Pattern '^\[verifica\]|^\[cpu\]|manca |init di|SBAGLIATA' | ForEach-Object { $_.Line } | Select-Object -First 40
if ($traccia) {
    foreach ($r in $err) {
        $m = [regex]::Match($r, '^\[os\] (q\d+) .*\[CMD_WRITE "([^"]*)"\]')
        if ($m.Success) { "voce $($m.Groups[1].Value): $($m.Groups[2].Value)" }
    }
}
Get-Content (Join-Path $b "out.txt")
if ($png -and (Test-Path (Join-Path $b "quadro.bmp"))) {
    Add-Type -AssemblyName System.Drawing
    $i = [Drawing.Image]::FromFile((Join-Path $b "quadro.bmp"))
    $i.Save($png, [Drawing.Imaging.ImageFormat]::Png)
    $i.Dispose()
}
