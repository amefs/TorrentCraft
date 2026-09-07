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

## 文件过滤

文件过滤是可选功能，仅在从目录制作种子时生效。明确选择单个文件制作种子时，
行为保持不变。

### 过滤模式

- `disabled`：包含目录中的所有条目；这是保持兼容性的默认模式。
- `common-artifacts`：排除内置的跨平台操作系统垃圾文件集合。
- `custom-rules`：只使用通过 `--exclude` 提供的规则；自定义规则会替换常见垃圾文件
  集合，而不是在其上追加。

内置的常见操作系统垃圾文件规则如下：

~~~text
.DS_Store
._*
Icon\r
__MACOSX/
.Spotlight-V100/
.Trashes/
.fseventsd/
Thumbs.db
ehthumbs.db
desktop.ini
$RECYCLE.BIN/
System Volume Information/
~~~

该列表不会笼统排除所有隐藏文件、`.git`、`.gitignore`、`.env` 或构建目录。这些文件
可能是有意义的内容，如需排除必须添加明确的自定义规则。

### 自定义 Glob 规则

规则使用 TorrentCraft CLI、GUI、配置和 Core 共享的简化 Glob 语法：

- `/` 分隔路径组件；反斜杠 `\` 会规范化为 `/`。
- 不含 `/` 的模式会匹配任意目录层级中的同名文件或目录。
- `*` 和 `?` 在单个路径组件内匹配；`**` 可以跨越多个路径组件。
- 以 `/` 结尾的规则只匹配目录，例如 `cache/`。
- 不支持 gitignore 的否定、锚定语义，也不支持 rsync 的传输过滤语法。
- 默认不区分大小写；需要精确大小写时使用 `--filter-case-sensitive`，也可以用
  `--filter-case-insensitive` 显式指定默认策略。
- 空规则、只含空白的规则以及含 NUL 字节的规则会在规划和写入前被拒绝。

例如，以下命令会排除临时文件和缓存目录，同时保留其他内容：

~~~bash
torrentcraft create ./payload -o ./payload.torrent \
  --filter-mode custom-rules \
  --exclude '*.tmp' --exclude 'cache/' \
  --filter-case-sensitive
~~~

### 过滤报告

目录制作成功后以及 `--dry-run` 试运行中，都会报告被排除的条目。普通文本输出会列出
每条记录的相对路径、条目类型、匹配规则和字节摘要。匹配到的目录只报告一次，并附带
其后代条目数和后代普通文件字节数。

使用 `--json` 时，结果包含：

~~~json
{
  "data": {
    "filtered": {
      "count": 1,
      "bytes": 4096,
      "entries": [
        {
          "path": "cache",
          "kind": "directory",
          "rule": "cache/",
          "bytes": 4096,
          "descendant_count": 3,
          "descendant_bytes": 4096
        }
      ]
    }
  }
}
~~~

报告只说明哪些内容没有写入种子，不会删除或修改源文件系统。

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
显式命令行参数 > 显式预设 > 默认预设 > 全局配置默认值 > 内置引擎默认值
```

未提供 `--preset` 或 `--preset-file` 时，顶层配置项 `default_preset` 会选择默认预设并
叠加到全局配置默认值之上。CLI 与 GUI 的 create/dry-run 使用相同的解析顺序；显式预设会
替换这个默认预设。

对于 `created_by`，`--created-by` 是最高优先级的显式值；未指定时依次使用选中的预设
（包括 `default_preset` 默认预设）、全局配置默认值，最后回退到 `TorrentCraft`。完整的
CLI/GUI 解析图请参见[制作设置的解析顺序](./config#制作设置的解析顺序)。

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
| `--filter-mode MODE` | 文件过滤模式：`disabled`、`common-artifacts` 或 `custom-rules`。 |
| `--exclude PATTERN` | 添加自定义 Glob 排除规则；可重复传入，并选择 `custom-rules` 模式。 |
| `--filter-case-sensitive` | 启用区分大小写的过滤规则匹配。 |
| `--filter-case-insensitive` | 关闭大小写区分（默认行为）。 |
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
