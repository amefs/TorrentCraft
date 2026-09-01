# 创建种子 (create)

使用 `create` 命令从单个文件或整个目录快速制作 `.torrent` 种子文件。

```text
torrentcraft create <content> -o <target.torrent> [options]
```

只有在所有源文件读取和哈希计算全部成功后，才会正式写入目标文件。若目标文件已存在，工具会严格进行防误触保护并拒绝覆盖，除非显式传入 `--overwrite` 参数。

## 常用示例

```bash
# 1. 自动计算最合适的分块大小，制作 Hybrid 混合种子
torrentcraft create ./my-folder -o ./my-folder.torrent

# 2. 指定 1 MiB（1024 KiB）固定分块大小制作 v1 种子
torrentcraft create ./my-folder -o ./my-folder.torrent \
  --format v1 --piece-size 1024

# 3. 试运行（Dry-run）：仅校验参数与文件，不实际生成文件
torrentcraft create ./my-folder -o ./my-folder.torrent --dry-run

# 4. 输出供脚本解析的结构化 JSON 数据
torrentcraft create ./my-folder -o ./my-folder.torrent --json
```

## 协议格式与分块大小

- `--format`：支持 `v1`、`v2` 或 `hybrid`，默认推荐 `hybrid`。
  - **`v1`**：传统的 BitTorrent v1 格式（基于 SHA-1 哈希），兼容所有新旧客户端。
  - **`v2`**：新一代 BitTorrent v2 规范（每个文件基于 SHA-256 Merkle 哈希树），安全性更高。
  - **`hybrid`**：同时包含 V1 和 V2 双哈希树，新旧客户端均可无缝加入同一个做种集群（Swarm）。
- `--piece-size`：接受 `auto` 或以 KiB 为单位的整数（如 `512`、`1024`、`2048`、`4096` 等）。固定值必须是 16 KiB 到 16384 KiB（16 MiB）之间的 2 的幂。默认值为 `auto`（根据数据总量自动匹配最合适的分块大小）。

## 文件排序策略

`--file-order` 控制文件在种子内部字典中的排列顺序：

- `lexicographical`：按原始字节序排列（默认值，符合标准规范）。
- `natural`：自然排序，文件名中的数字按数值大小排列（如 `file2` 排在 `file10` 前面），不区分大小写。
- `canonical_alignment`：遵循 BitTorrent 规范对齐策略排序。
- `breadth_first`：广度优先排序，先按目录层级深度排列，再按字母序排列。

```bash
torrentcraft create ./电视剧合集 -o ./电视剧合集.torrent \
  --format hybrid --piece-size auto --file-order natural
```

## Tracker 服务器与 Web Seed

添加 Tracker 服务器并按主备层级分组：

```bash
torrentcraft create ./payload -o ./payload.torrent \
  --tracker 'https://tracker1.example/announce' --tier 0 \
  --tracker 'https://tracker2.example/announce' --tier 0 \
  --tracker 'https://backup.example/announce' --tier 1 \
  --web-seed 'https://cdn.example/payload.zip'
```

- 每个 `--tracker` 后可紧跟 `--tier N` 指定层级（0 到 64；省略时默认为 Tier 0）。
- 只要在命令行中传入了任何 `--tracker`，就会整体替换配置文件中继承的默认 Tracker 列表。
- `--web-seed` 可多次传入，添加 HTTP/HTTPS 直接下载源。

## 元数据与私有种子标记

```bash
torrentcraft create ./数据集 -o ./数据集.torrent \
  --comment '官方正式发布版本' \
  --created-by 'MyTeam 发布组' \
  --creation-date 1735689600 \
  --source 'internal-repo' \
  --private
```

- `--private`：标记为私有种子（Private Torrent），客户端会自动禁用 DHT、PEX 和本地节点发现，专为 PT 站点设计。
- `--comment`：写入描述性注释文字。
- `--created-by`：写入制作者/发布工具签名。
- `--creation-date`：指定创建时间戳（Unix 秒数；省略时使用当前时间）。
- `--source`：写入 `info["source"]` 来源标识（常用于 PT 站区分不同站点；会影响 Info Hash）。

## 配置文件与预设模板

无需每次输入大量参数，你可以直接复用 `torrentcraft.json` 中配置的命名预设：

```bash
# 使用配置文件中名为 release 的预设模板
torrentcraft create ./payload -o ./payload.torrent --preset release

# 指定自定义配置文件中的预设
torrentcraft create ./payload -o ./payload.torrent \
  --config ./custom-config.json --preset release

# 直接读取外部预设 JSON 文件
torrentcraft create ./payload -o ./payload.torrent \
  --preset-file ./preset_release.json
```

### 参数生效优先级

```text
命令行参数 > 选中的预设模板 > 全局配置默认值 > 内置引擎默认值
```

## 性能与进度显示

- `--memory-working-set-limit SIZE`：限制进程工作集内存占用（默认 `512MiB`，主要用于 Windows）。
- `--progress <mode>`：控制写入标准错误的进度模式（`plain` 纯文本逐行、`tty` 终端动态条、`json` 事件流）。
- `--quiet`：静默模式，成功时不输出非必要的提示信息。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `-o`, `--output PATH` | 目标 `.torrent` 文件的输出路径（必填）。 |
| `--format v1\|v2\|hybrid` | 选择协议格式（默认：`hybrid`）。 |
| `--piece-size KIB\|auto` | 选择分块大小（KiB）或 `auto` 自动计算（默认：`auto`）。 |
| `--file-order POLICY` | 文件排序策略：`lexicographical`、`natural`、`canonical_alignment`、`breadth_first`。 |
| `--private`, `--no-private`, `--public` | 设置或清除私有种子标记。 |
| `--tracker URL` | 添加 Tracker 地址（可后接 `--tier N`，可重复传入）。 |
| `--web-seed URL` | 添加 Web Seed 做种源（可重复传入）。 |
| `--comment TEXT` | 设置顶层注释内容。 |
| `--created-by TEXT` | 设置顶层制作者签名。 |
| `--creation-date N` | 设置创建时间戳（Unix 秒数）。 |
| `--source TEXT` | 设置 `info["source"]` 来源字段。 |
| `--preset NAME` | 应用配置文件中的命名预设。 |
| `--preset-file PATH` | 读取外部预设 JSON 文件。 |
| `--config PATH` | 指定自定义 `torrentcraft.json` 路径。 |
| `--memory-working-set-limit SIZE` | 限制进程工作集内存大小（如 `512MiB`）。 |
| `--overwrite` | 允许覆盖已存在的输出文件。 |
| `--dry-run` | 仅做校验预检，不实际生成文件。 |
| `--quiet` | 抑制多余的控制台文本输出。 |
| `--json` | 输出结构化 JSON 响应。 |
| `--progress MODE` | 进度显示模式（`json`、`plain`、`tty`）。 |
| `-h`, `--help` | 显示帮助信息。 |
