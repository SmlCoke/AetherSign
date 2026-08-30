# AetherSign 运行资产接口指南

## I. 开发者下载接口

### 1.1 稳定资产名称

资产文件名在后续 Release 中保持不变，从而同时支持固定 tag 和 latest URL：

| Checkpoint | Release Asset |
| --- | --- |
| `final` | `aethersign-app-assets-final.zip` |
| `half-final` | `aethersign-app-assets-half-final.zip` |
| `preminilary` | `aethersign-app-assets-preminilary.zip` |
| `vertical` | `aethersign-app-assets-vertical.zip` |
| 全局清单 | `aethersign-app-assets-manifest.json` |
| 校验和 | `SHA256SUMS.txt` |

### 1.2 固定版本下载

固定 tag URL 适合可复现构建：

```text
https://github.com/SmlCoke/AetherSign/releases/download/v1.0.0/aethersign-app-assets-final.zip
```

将文件名替换为表格中的其他资产即可。

### 1.3 最新正式版本下载

Latest URL 适合始终获取最新正式版，但不会解析 draft 或 prerelease：

```text
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-app-assets-final.zip
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-app-assets-manifest.json
https://github.com/SmlCoke/AetherSign/releases/latest/download/SHA256SUMS.txt
```

GitHub 对 latest asset URL 的官方说明见 [Linking to releases](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)。

## 2. 下载、校验和安装

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

安装后仍需从合法 SDK 环境补齐 `app_assets/colorLUT.sscl`，以及公共仓库未分发的厂商集成文件。
