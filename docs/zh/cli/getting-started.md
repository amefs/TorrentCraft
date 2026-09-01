# 开始使用

## 获取与安装

TorrentCraft 为 Linux (x86_64) 和 Windows (x86_64) 平台提供了免安装、开箱即用的独立二进制程序包，包含命令行工具和图形界面客户端：

- **命令行程序（CLI）**：`torrentcraft`（Linux） / `torrentcraft.exe`（Windows）
- **桌面图形界面（GUI）**：`torrentcraft-gui`（Linux） / `torrentcraft-gui.exe`（Windows）

请前往 GitHub 仓库的 Releases 页面下载最新发行版。

> **完整性校验**：每个版本均附带 `SHA256SUMS` 校验和文件，以及详尽的 SBOM（软件物料清单）和安全审计报告。你可以通过 `sha256sum -c SHA256SUMS`（Linux）或 `Get-FileHash`（PowerShell）核对下载文件的完整性。*（注：1.0.0 正式版本的二进制文件暂未签署代码数字签名，签名支持将在后续版本中推出）*。

## 验证安装

下载后解压并将程序放置于系统环境变量 `PATH` 路径（或当前工作目录）下，在终端中执行以下命令测试：

```bash
# 检查当前版本
torrentcraft --version

# 查看全局使用帮助
torrentcraft --help
```

每个子命令都可以通过 `--help` 查看专属参数说明与用法提示：

```bash
torrentcraft create --help
torrentcraft verify --help
```

## 5 分钟极速体验

下面通过一个最常见的实际场景，演示从制作种子、查看信息、打印目录树到完整性校验的全套流程：

```bash
# 1. 从本地文件夹制作一个 Hybrid（混合）格式的种子
torrentcraft create ./my-folder -o ./my-folder.torrent

# 2. 秒级查看种子的元数据、Info Hash 与配置信息
torrentcraft inspect ./my-folder.torrent

# 3. 打印种子内记录的文件目录树结构
torrentcraft tree ./my-folder.torrent

# 4. 比对本地文件夹与种子数据，校验文件完整性
torrentcraft verify ./my-folder.torrent ./my-folder
```

### 脚本编写与自动化集成建议

在 Shell 脚本或 CI 持续集成流程中调用 TorrentCraft 时：
- 为命令加上 `--json` 参数，即可获取结构化、便于代码解析的 JSON 数据包。
- 通过进程退出码判断执行状态（退出码为 `0` 代表成功，非 0 代表对应错误类型）。
- 详见[输出与退出码](./output)。

## 配置文件查找规则

CLI 工具与桌面 GUI 共享同一份配置文件（`torrentcraft.json`）。默认情况下，TorrentCraft 会按以下顺序自动查找配置：

1. 当前工作目录下的 `./torrentcraft.json`
2. 当前系统的用户配置目录：
   - **Linux / macOS**：`~/.config/torrentcraft/torrentcraft.json`
   - **Windows**：`%APPDATA%\torrentcraft\torrentcraft.json`

关于配置文件的格式与预设管理，请参阅[管理配置](./config)。

## 路径与特殊字符处理

支持相对路径（如 `./data`）和绝对路径（如 `/home/user/data` 或 `C:\Data`）。

- **空格与特殊符号**：当路径中包含空格或 Shell 特殊符号时，请务必用双引号包裹：
  ```bash
  torrentcraft create "./我的照片与视频" -o "./我的照片与视频.torrent"
  ```
- **中文字符与 Unicode**：TorrentCraft 在所有平台上均完整支持包含中文和 Unicode 字符的路径（在 Windows PowerShell 或 cmd 中亦可正常使用）。
