# 管理预设模板 (preset)

预设（Preset）是将常用的种子制作参数（如协议格式、分块大小、私有标记、文件排序策略和文件过滤等）保存为命名模板，存放在 `torrentcraft.json` 中，方便在制作种子时一键复用。

```text
torrentcraft preset list|show <name>|add <file>|remove <name> [options]
torrentcraft preset add --from-torrent <torrent> [--name NAME] [--fields LIST|all] [options]
```

## 1. 查看预设列表与详情

```bash
# 列出所有已保存的预设名称
torrentcraft preset list
torrentcraft preset list --json

# 查看指定预设的详细参数
torrentcraft preset show release
```

## 2. 导入预设 (`add`)

将预设配置保存为一个 JSON 文件（如 `preset_release.json`）：

```json
{
  "format": "hybrid",
  "piece_size": 4096,
  "private": true,
  "file_order": "natural",
  "file_filter": {
    "mode": "custom-rules",
    "case_sensitive": false,
    "patterns": ["*.tmp", "cache/"]
  }
}
```

导入到配置文件中：

```bash
torrentcraft preset add ./preset_release.json
```

- 预设名称会自动从文件名中提取并去除开头的 `preset_`（例如 `preset_release.json` 自动命名为 `release`）。
- 若预设名称已存在，需显式加上 `--force` 进行覆盖。

### 从已有种子导入预设

除了 JSON 文件，你还可以直接以现有的 `.torrent` 种子为模板提取预设。TorrentCraft 只会提取其中可复用的制作设置，而不会导入与具体文件绑定的信息（种子名称、文件列表、分块哈希、Info Hash、创建时间、DHT 节点与集合等）。

```bash
# 提取默认的可复用字段，并以种子文件名作为预设名
torrentcraft preset add --from-torrent ./release.torrent

# 试运行：预览将要保存的完整设置（含完整 Tracker URL），不实际写入
torrentcraft preset add --from-torrent ./release.torrent --dry-run

# 指定预设名称，并手动挑选要导入的字段
torrentcraft preset add --from-torrent ./release.torrent --name tracker-a \
  --fields trackers,piece-size,private,source
```

| 字段名 | 提取内容 | 默认勾选 |
| --- | --- | --- |
| `trackers` | Tracker 分层列表（`announce-list` 或单个 `announce`） | 是 |
| `piece-size` | 固定分块大小 `piece length`（须为 16 KiB 至 16 MiB 之间的 2 的幂） | 是 |
| `private` | `info.private` 私有标记（缺失时视为 `false`） | 是 |
| `source` | `info.source` 来源标识（忽略顶层 `source` 字段） | 是 |
| `web-seeds` | HTTP/HTTPS `url-list` 做种源列表 | 是 |
| `format` | 种子声明的协议格式（`v1`、`v2` 或 `hybrid`） | 是 |
| `comment` | 顶层 `comment` 注释 | 否 |
| `created-by` | 顶层 `created by` 制作者签名 | 否 |

导入规则说明：

- **未勾选的字段**不会写入预设，制作种子时仍按常规顺序回退到 `default_preset`、全局配置默认值与内置默认值。
- **缺失或不支持的字段**（例如种子没有 Tracker 或 Web Seed、文本无法解码、分块大小不在支持范围内）会给出警告并自动跳过，无论是默认勾选还是通过 `--fields` 指定。只有当所有选定字段都无法提取时，命令才会报错失败。
- **容忍非标元数据**：未知字段与 `x-*` 扩展键会被直接忽略；无效的 Tracker 或 Web Seed URL 会被剔除并保留有效地址；非 UTF-8 文本仅使对应字段不可用（以上均以警告提示）。只有本身损坏或无法安全解析的种子（Bencode 损坏、`info` 字典或分块信息无效、包含不安全路径）才会被拒绝。
- **`--json` 输出**会一并返回已保存的 `settings`、`load_warnings` 与 `skipped_fields`。
- **原样保留 passkey**：Tracker URL（包括私人站点的 passkey）会原样保存在 `torrentcraft.json` 中。
- **图形界面操作**：在桌面 GUI 中点击菜单栏 **Preset → Import From File**（**预设 → 从文件导入**），选择 `.torrent` 文件后即可在 **从种子导入（Import from torrent）** 窗口中勾选要保留的字段并保存（参见[桌面 GUI](./gui#预设模板-preset-管理)）。

## 3. 删除预设 (`remove`)

```bash
torrentcraft preset remove release
```

## 4. 在制作种子时调用预设

```bash
# 制作种子时直接应用 release 预设
torrentcraft create ./payload -o ./payload.torrent --preset release
```

### 参数生效优先级

```text
显式命令行参数 > 显式预设 > 默认预设 > 全局配置默认值 > 内置引擎默认值
```

顶层配置项 `default_preset` 仅在没有提供 `--preset` 或 `--preset-file` 时生效。它是 CLI 与
GUI 共享的 create 默认预设，会叠加到全局配置默认值之上；显式预设会替换它。

对于 `created_by`，选中的预设值（包括 `default_preset` 默认预设）会覆盖全局配置默认值。
CLI 的 `--created-by` 参数属于最高优先级的显式覆盖，显式空字符串也会被保留。完整的
解析图请参见[制作设置的解析顺序](./config#制作设置的解析顺序)。


## 文件过滤覆盖规则

预设中的 `file_filter` 是一个完整策略对象。预设未声明它时，继承
`defaults.file_filter`；声明后会替换完整的全局策略，包括模式、大小写策略和规则列表。
如需让该预设明确关闭过滤，请使用 `mode: "disabled"`。

命令行过滤参数在选中预设后生效，并且仍然是按字段覆盖。例如，`--filter-case-sensitive`
只改变大小写匹配策略，不会改变最终模式和规则列表。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--config PATH` | 指定自定义 `torrentcraft.json` 路径。 |
| `--force` | 导入时允许覆盖同名的已有预设。 |
| `--from-torrent PATH` | 从现有 `.torrent` 种子文件中提取可复用的制作设置并保存为预设。 |
| `--name NAME` | 指定 `add` 保存的预设名称（默认取文件名主干）。 |
| `--fields LIST\|all` | 配合 `--from-torrent` 使用，以逗号分隔指定要导入的字段（或 `all`）。 |
| `--dry-run` | 试运行：仅校验修改，不写入文件。 |
| `--backup` | 写入前自动生成 `.bak` 备份文件。 |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
