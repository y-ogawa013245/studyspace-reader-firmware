# StudySpace ATOMS3 カードリーダー ワンクリックインストーラー (Windows / macOS) ZIP生成スクリプト
param(
    [string]$FirmwareRepoDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$ServerDownloadsDir = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path "server\public\downloads")
)

$ErrorActionPreference = "Stop"

Write-Host "=== StudySpace ATOMS3 Installer Package Builder ==="
Write-Host "Firmware Dir  : $FirmwareRepoDir"
Write-Host "Downloads Dir : $ServerDownloadsDir"

if (-not (Test-Path $ServerDownloadsDir)) {
    New-Item -ItemType Directory -Path $ServerDownloadsDir -Force | Out-Null
}

$installerDir = Join-Path $FirmwareRepoDir "installer"
$sketchDir    = Join-Path $FirmwareRepoDir "atoms3_nfc_reader"
$csc          = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"

# アイコンファイル探索
$icoPath = Join-Path $installerDir "app.ico"
if (-not (Test-Path $icoPath)) {
    $fallbackIco = Join-Path (Split-Path $FirmwareRepoDir -Parent) "reader-agent\app.ico"
    if (Test-Path $fallbackIco) {
        Copy-Item -Path $fallbackIco -Destination $icoPath -Force
    }
}

# 1. Windows 用 setup.exe のコンパイル
$setupExeOut = Join-Path $installerDir "setup.exe"
$csSource    = Join-Path $installerDir "Atoms3Installer.cs"

$cscArgs = @("/nologo", "/codepage:65001", "/target:winexe", "/out:$setupExeOut", $csSource)
if (Test-Path $icoPath) {
    $cscArgs = @("/nologo", "/codepage:65001", "/target:winexe", "/win32icon:$icoPath", "/out:$setupExeOut", $csSource)
}

Write-Host "Compiling Atoms3Installer.cs -> setup.exe ..."
& $csc @cscArgs
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $setupExeOut)) {
    throw "setup.exe のコンパイルに失敗しました。"
}

# README.txt 生成
$readmeText = @"
====================================================================
  StudySpace ATOMS3 単体カードリーダー セットアップガイド
====================================================================

【構成部品】
- マイコン本体 : M5Stack ATOMS3 Lite (SKU: C124)
- 拡張ベース   : M5Stack Atomic Port ABC Base (SKU: A130)
- PORT A (赤)  : M5Stack NFC Universal Unit (ST25R3916 / SKU: U216)
- PORT B (黒)  : M5Stack Unit Buzzer (SKU: U132) ※任意

【Windows での簡単セットアップ手順】
1. このZIPファイルを右クリックして「すべて展開」で解凍します。
2. ATOMS3 Lite を USB Type-C ケーブルで PC に接続します。
3. フォルダ内の「setup.exe」をダブルクリックして起動します。
4. 画面上で「拠点・設置場所名（例: 那覇校 1F自習室）」と「Wi-FiのSSID/パスワード」を
   入力し、【ATOMS3 にインストール ＆ 初期設定を書き込む】をクリックします。

【スマホから後でWi-Fiや拠点名を変更したい場合】
- ATOMS3 Lite の本体ボタン（中央レンズ部分）を「2秒以上長押し」すると、
  本体LEDが青白く光り、「StudySpace-Setup-XXXX」というWi-Fiが発信されます。
- スマホでそのWi-Fiに接続し、ブラウザで http://192.168.4.1 を開くと、
  いつでも拠点名・Wi-Fi・サーバーURL・ブザーON/OFFを変更できます。
"@

Add-Type -AssemblyName System.IO.Compression.FileSystem

# ====================================================================
# Windows 版パッケージ作成 (studyspace-atoms3-installer-win.zip)
# ====================================================================
$tempWin = Join-Path $env:TEMP "studyspace_atoms3_win_pkg"
if (Test-Path $tempWin) { Remove-Item -Recurse -Force $tempWin }
$coreWin = Join-Path $tempWin ".core"
New-Item -ItemType Directory -Path $coreWin -Force | Out-Null

Copy-Item -Path $setupExeOut -Destination (Join-Path $tempWin "setup.exe") -Force
Set-Content -Path (Join-Path $tempWin "README.txt") -Value $readmeText -Encoding UTF8

if (Test-Path $icoPath) {
    Copy-Item -Path $icoPath -Destination (Join-Path $coreWin "app.ico") -Force
}

# ビルド済み firmware_merged.bin や esptool.exe があれば同梱
$mergedBin = Join-Path $FirmwareRepoDir "build\firmware_merged.bin"
if (Test-Path $mergedBin) {
    Copy-Item -Path $mergedBin -Destination (Join-Path $coreWin "firmware_merged.bin") -Force
}
$esptoolBin = Join-Path $installerDir "esptool.exe"
if (Test-Path $esptoolBin) {
    Copy-Item -Path $esptoolBin -Destination (Join-Path $coreWin "esptool.exe") -Force
}

# Arduino IDE でも開けるようソースコード一式も同梱
Copy-Item -Recurse -Path $sketchDir -Destination (Join-Path $coreWin "atoms3_nfc_reader") -Force

$zipWinPath = Join-Path $ServerDownloadsDir "studyspace-atoms3-installer-win.zip"
if (Test-Path $zipWinPath) { Remove-Item -Force $zipWinPath }
[System.IO.Compression.ZipFile]::CreateFromDirectory($tempWin, $zipWinPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

# 既存エンドポイント互換用にもコピー
Copy-Item -Path $zipWinPath -Destination (Join-Path $ServerDownloadsDir "studyspace-reader-agent-win.zip") -Force
Copy-Item -Path $zipWinPath -Destination (Join-Path $ServerDownloadsDir "studyspace-reader-agent.zip") -Force
Remove-Item -Recurse -Force $tempWin

# ====================================================================
# macOS 版パッケージ作成 (studyspace-atoms3-installer-mac.zip)
# ====================================================================
$tempMac = Join-Path $env:TEMP "studyspace_atoms3_mac_pkg"
if (Test-Path $tempMac) { Remove-Item -Recurse -Force $tempMac }
$coreMac = Join-Path $tempMac ".core"
New-Item -ItemType Directory -Path $coreMac -Force | Out-Null

Copy-Item -Path (Join-Path $installerDir "setup_mac.command") -Destination (Join-Path $tempMac "setup.command") -Force
Set-Content -Path (Join-Path $tempMac "README.txt") -Value $readmeText -Encoding UTF8
if (Test-Path $mergedBin) {
    Copy-Item -Path $mergedBin -Destination (Join-Path $coreMac "firmware_merged.bin") -Force
}
Copy-Item -Recurse -Path $sketchDir -Destination (Join-Path $coreMac "atoms3_nfc_reader") -Force

$zipMacPath = Join-Path $ServerDownloadsDir "studyspace-atoms3-installer-mac.zip"
if (Test-Path $zipMacPath) { Remove-Item -Force $zipMacPath }
[System.IO.Compression.ZipFile]::CreateFromDirectory($tempMac, $zipMacPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)
Copy-Item -Path $zipMacPath -Destination (Join-Path $ServerDownloadsDir "studyspace-reader-agent-mac.zip") -Force
Remove-Item -Recurse -Force $tempMac

Write-Host "✅ Generated packages successfully:"
Get-Item (Join-Path $ServerDownloadsDir "studyspace-atoms3-installer-*.zip") | Select-Object Name, Length
