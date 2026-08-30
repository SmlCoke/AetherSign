# AetherSign 运行资产与模型归档接口指南

## I. 开发者下载接口

### 1.1 稳定资产名称

资产文件名在后续 Release 中保持不变，从而同时支持固定 tag 和 latest URL：

| Checkpoint | Release Asset |
| --- | --- |
| `final` | `aethersign-app-assets-final.zip` |
| `half-final` | `aethersign-app-assets-half-final.zip` |
| `preminilary` | `aethersign-app-assets-preminilary.zip` |
| `vertical` | `aethersign-app-assets-vertical.zip` |
| 项目模型归档 | `aethersign-models-archive.zip` |
| 全局清单 | `aethersign-app-assets-manifest.json` |
| 校验和 | `SHA256SUMS.txt` |

四个 `app-assets` ZIP 用于补齐对应 checkpoint 的板端运行目录；`aethersign-models-archive.zip` 则完整保留仓库本地 `models/` 的版本结构，用于模型归档、分析和复现，不应直接复制到单个 checkpoint 的 `app_assets/`。全局清单沿用原文件名以保持下载接口兼容。

### 1.2 固定版本下载

固定 tag URL 适合可复现构建：

```text
https://github.com/SmlCoke/AetherSign/releases/download/v1.0.0/aethersign-app-assets-final.zip
https://github.com/SmlCoke/AetherSign/releases/download/v1.0.0/aethersign-models-archive.zip
```

将文件名替换为表格中的其他资产即可。

### 1.3 最新正式版本下载

Latest URL 适合始终获取最新正式版，但不会解析 draft 或 prerelease：

```text
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-app-assets-final.zip
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-models-archive.zip
https://github.com/SmlCoke/AetherSign/releases/latest/download/aethersign-app-assets-manifest.json
https://github.com/SmlCoke/AetherSign/releases/latest/download/SHA256SUMS.txt
```

GitHub 对 latest asset URL 的官方说明见 [Linking to releases](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases)。

## 2. Checkpoint 运行资产：下载、校验和安装

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

## 3. 项目模型归档：下载与解压

模型归档以 `models/` 为目录根，保留当前归档中的 `final/`、`half_final/` 和历史拼写 `perminlary/` 层级，以及其中的 ONNX、A1 `.m1model` 和相关团队原创模型定义/推理文件。该归档不包含训练数据集、厂商 SDK 或其他厂商材料。

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

解压后模型文件位于 `aethersign-models-v1.0.0/models/`。每个 ZIP 内的 `manifest.json` 提供文件级 SHA-256；全局 `aethersign-app-assets-manifest.json` 和 `SHA256SUMS.txt` 覆盖本 Release 的全部归档。
