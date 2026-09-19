# Assemble the @Mod folders for publishing.
#
# The @Mod folders are build output and are not in git. mod_build writes
# addons/ into them and signs it; this puts the rest in place -- the files a
# published mod needs that no build step produces.
#
# Run it after mod_build and before Publisher. Running it twice is harmless.
#
#   .\package.ps1            assemble every mod
#   .\package.ps1 -Check     say what is missing or stale, change nothing
#
# WHY THIS EXISTS AT ALL. mod.cpp and meta.cpp used to be committed inside the
# @Mod folders, which put source and build output in one directory and meant a
# deleted build folder took the Workshop item id with it. Moving them into
# packaging/ fixes that and creates one new risk in its place: a folder
# assembled by hand can be published with a stale mod.cpp. This script is the
# answer to that risk, so use it rather than copying by hand.

[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$src  = Join-Path $root 'packaging'
$keys = Join-Path $root 'keys'

if (-not (Test-Path $src)) { throw "no packaging/ directory beside this script" }

$problems = 0
$copied   = 0

foreach ($dir in Get-ChildItem $src -Directory) {
    $mod = $dir.Name
    $dst = Join-Path $root "@$mod"

    # A @Mod folder with no addons/ has not been built yet. Say so rather than
    # assembling half of one: a folder that looks ready and carries no pbo is
    # the sort of thing that gets published.
    $addons = Join-Path $dst 'addons'
    if (-not (Test-Path $addons) -or -not (Get-ChildItem $addons -Filter *.pbo -ErrorAction SilentlyContinue)) {
        Write-Host "  $mod : not built yet (no pbo in @$mod/addons) -- run mod_build first" -ForegroundColor Yellow
        $problems++
        continue
    }

    foreach ($file in Get-ChildItem $dir.FullName -File) {
        $target = Join-Path $dst $file.Name
        $same = (Test-Path $target) -and
                ((Get-FileHash $target).Hash -eq (Get-FileHash $file.FullName).Hash)

        if ($same) { continue }

        if ($Check) {
            Write-Host "  $mod : $($file.Name) differs from packaging/" -ForegroundColor Yellow
            $problems++
        } else {
            Copy-Item $file.FullName $target -Force
            Write-Host "  $mod : $($file.Name)" -ForegroundColor Green
            $copied++
        }
    }

    # The public key travels with the mod so a server running
    # verifySignatures = 2 can install it. mod_build already copies it during
    # signing; this only covers a folder assembled without a signing build.
    $bikey = Get-ChildItem $keys -Filter *.bikey -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($bikey) {
        $kdst = Join-Path $dst 'keys'
        $ktarget = Join-Path $kdst $bikey.Name
        if (-not (Test-Path $ktarget)) {
            if ($Check) {
                Write-Host "  $mod : keys/$($bikey.Name) missing" -ForegroundColor Yellow
                $problems++
            } else {
                New-Item -ItemType Directory -Path $kdst -Force | Out-Null
                Copy-Item $bikey.FullName $ktarget -Force
                Write-Host "  $mod : keys/$($bikey.Name)" -ForegroundColor Green
                $copied++
            }
        }
    } else {
        Write-Host "  $mod : no .bikey in keys/ -- the pbo will be unsigned" -ForegroundColor Yellow
        $problems++
    }
}

Write-Host ""
if ($Check) {
    if ($problems -eq 0) { Write-Host "every @Mod folder matches packaging/" -ForegroundColor Green }
    else { Write-Host "$problems thing(s) to fix -- run .\package.ps1 without -Check" -ForegroundColor Yellow }
} else {
    Write-Host "$copied file(s) placed."
    if ($problems -gt 0) { Write-Host "$problems mod(s) skipped, see above." -ForegroundColor Yellow }
}

exit 0
