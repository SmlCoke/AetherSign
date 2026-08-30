# AetherSign Runtime Asset and Model Archive Interface Guide

## I. Developer Download Interface

### 1.1 Stable Asset Names

Asset filenames remain unchanged across subsequent releases, supporting both fixed-tag and "latest" URLs:

| Checkpoint | Release Asset |
| --- | --- |
| `final` | `aethersign-app-assets-final.zip` |
| `half-final` | `aethersign-app-assets-half-final.zip` |
| `preminilary` | `aethersign-app-assets-preminilary.zip` |
| `vertical` | `aethersign-app-assets-vertical.zip` |
| Project model archive | `aethersign-models-archive.zip` |
| Global Manifest | `aethersign-app-assets-manifest.json` |
| Checksum | `SHA256SUMS.txt` |

The four `app-assets` ZIP files restore the runtime directory of their respective checkpoints. `aethersign-models-archive.zip` instead preserves the complete versioned structure of the local `models/` directory for archival, analysis, and reproduction; it must not be copied directly into a single checkpoint's `app_assets/`. The global manifest keeps its existing filename for download-interface compatibility.

### 1.2 Fixed-Version Download

Fixed-tag URLs are suitable for reproducible builds:

```text
https://github.com/SmlCoke/AetherSign/releases/download/v1.0.0/aethersign-app-assets-final.zip
https://github.com/SmlCoke/AetherSign/releases/download/v1.0.0/aethersign-models-archive.zip
```

Simply replace the filename with other assets listed in the table.

### 1.3 Latest Official Version Download

"Latest" URLs are suitable for always fetching the most recent official release; note that these do not resolve to drafts or pre-releases:

```text
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-app-assets-final.zip
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-models-archive.zip
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-app-assets-manifest.json
https://github.com/SmlCoke/AetherSign/releases/latest/download/SHA256SUMS.txt
```

For GitHub's official documentation on "latest" asset URLs, see [Linking to releases](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases).

## 2. Checkpoint runtime assets: download, verify, and install

### 2.1 PowerShell

```powershell
$Tag = 'v1.0.0'
$Checkpoint = 'final'
$Name = "aethersign-app-assets-$Checkpoint.zip"
$Base = "https://github.com/SmlCoke/AetherSign/releases/download/$Tag"

Invoke-WebRequest "$Base/$Name" -OutFile $Name
Invoke-WebRequest "$Base/SHA256SUMS.txt" -OutFile 'SHA256SUMS.txt'

$Expected = ((Get-Content 'SHA256SUMS.txt' | Where-Object { $_ -match "  $([regex]::Escape($Name))$" }) -split '\s+')[0]
$Actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Name).Hash.ToLowerInvariant()
if ($Actual -ne $Expected) {
    throw "SHA-256 mismatch for $Name"
}

$Extracted = Join-Path $PWD "aethersign-assets-$Checkpoint"
Expand-Archive -LiteralPath $Name -DestinationPath $Extracted -Force
Copy-Item -Path "$Extracted\app_assets\*" `
    -Destination ".\src\ssne_ai_demo\bak\$Checkpoint\app_assets" `
    -Recurse -Force
```

### 2.2 Bash

```bash
tag=v1.0.0
checkpoint=final
name="aethersign-app-assets-${checkpoint}.zip"
base="https://github.com/SmlCoke/AetherSign/releases/download/${tag}"

curl -fL -o "$name" "$base/$name"
curl -fL -o SHA256SUMS.txt "$base/SHA256SUMS.txt"
grep "  ${name}$" SHA256SUMS.txt | sha256sum --check -

rm -rf "aethersign-assets-${checkpoint}"
mkdir "aethersign-assets-${checkpoint}"
unzip "$name" -d "aethersign-assets-${checkpoint}"
cp -a "aethersign-assets-${checkpoint}/app_assets/." \
  "src/ssne_ai_demo/bak/${checkpoint}/app_assets/"
```

After installation, you still need to copy `app_assets/colorLUT.sscl` from a valid SDK environment, as well as vendor integration files that are not distributed in the public repository.

## 3. Project model archive: download and extract

The model archive uses `models/` as its root and preserves the current archive's `final/`, `half_final/`, and historically spelled `perminlary/` hierarchy. It contains ONNX models, A1 `.m1model` binaries, and related team-created model definition or inference files. It does not contain training datasets, the vendor SDK, or other vendor materials.

### 3.1 PowerShell

```powershell
$Tag = 'v1.0.0'
$Name = 'aethersign-models-archive.zip'
$Base = "https://github.com/SmlCoke/AetherSign/releases/download/$Tag"

Invoke-WebRequest "$Base/$Name" -OutFile $Name
Invoke-WebRequest "$Base/SHA256SUMS.txt" -OutFile 'SHA256SUMS.txt'

$Expected = ((Get-Content 'SHA256SUMS.txt' | Where-Object { $_ -match "  $([regex]::Escape($Name))$" }) -split '\s+')[0]
$Actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Name).Hash.ToLowerInvariant()
if ($Actual -ne $Expected) {
    throw "SHA-256 mismatch for $Name"
}

Expand-Archive -LiteralPath $Name -DestinationPath '.\aethersign-models-v1.0.0' -Force
```

### 3.2 Bash

```bash
tag=v1.0.0
name=aethersign-models-archive.zip
base="https://github.com/SmlCoke/AetherSign/releases/download/${tag}"

curl -fL -o "$name" "$base/$name"
curl -fL -o SHA256SUMS.txt "$base/SHA256SUMS.txt"
grep "  ${name}$" SHA256SUMS.txt | sha256sum --check -

rm -rf aethersign-models-v1.0.0
mkdir aethersign-models-v1.0.0
unzip "$name" -d aethersign-models-v1.0.0
```

After extraction, model files are located under `aethersign-models-v1.0.0/models/`. The `manifest.json` inside each ZIP provides file-level SHA-256 values; the global `aethersign-app-assets-manifest.json` and `SHA256SUMS.txt` cover every archive in the Release.
