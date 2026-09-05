# 管理配置 (config)

TorrentCraft 使用规范的 JSON 配置文件 `torrentcraft.json`，在 CLI 命令行与桌面 GUI 客户端之间无缝共享。

```text
torrentcraft config path|show|init|get <key>|set <key> <value> [options]
```

## 配置文件查找顺序

当未通过 `--config PATH` 明确指定路径时，TorrentCraft 会按以下优先级自动查找配置文件：

1. 当前工作目录下的 `./torrentcraft.json`
2. 系统用户配置目录：
   - **Linux / macOS**：`$XDG_CONFIG_HOME/torrentcraft/torrentcraft.json`（未设置 XDG 时使用 `$HOME/.config/torrentcraft/torrentcraft.json`）
   - **Windows**：`%APPDATA%\torrentcraft\torrentcraft.json`

显式指定的路径或已发现的配置文件具有权威性。如果文件中存在格式错误或未知字段，命令会立即报错提示，绝不会静默忽略或回退。

## 常用操作

```bash
# 查看当前正在生效的配置文件路径
torrentcraft config path

# 查看完整的配置内容（JSON 格式）
torrentcraft config show --json

# 初始化生成一份标准的配置文件模板
torrentcraft config init --config ./torrentcraft.json

# 强制覆盖已存在的配置文件
torrentcraft config init --config ./torrentcraft.json --force
```

## 读取与修改配置项

配置键名采用点号（dot-notation）层级路径，支持管理制作默认值、校验性能参数及命名预设：

```bash
# 查询指定配置项
torrentcraft config get defaults.piece_size

# 修改配置项（值需符合 JSON 语法）
torrentcraft config set defaults.private true
torrentcraft config set verify.workers 4
torrentcraft config set verify.memory '"64 MiB"'
torrentcraft config set disk_io '"mmap"'

# 赋值为 null 即可删除该配置项
torrentcraft config set defaults.comment null
```

## 完整配置文件范例

```json
{
  "schema": "torrentcraft.config/v1",
  "defaults": {
    "format": "hybrid",
    "piece_size": "auto",
    "private": false,
    "created_by": "TorrentCraft",
    "file_order": "lexicographical",
    "file_filter": {
      "mode": "common-artifacts",
      "case_sensitive": false,
      "patterns": []
    },
    "tracker_list": [],
    "web_seeds": []
  },
  "presets": {
    "release": {
      "piece_size": 4096,
      "private": true
    }
  },
  "verify": {
    "workers": 1,
    "memory": "32 MiB"
  },
  "disk_io": "mmap",
  "memory_working_set_limit": "512 MiB"
}
```

- **`defaults`**：制作新种子时的全局默认参数。
- **`presets`**：命名预设模板，可通过 `--preset <name>` 随时调用。
- **`verify` / `disk_io` / `memory_working_set_limit`**：全局校验线程数、缓存大小及磁盘 I/O 模式设置。

## 文件过滤默认值与预设覆盖

可选的 `defaults.file_filter` 对象控制目录制作时的全局过滤默认值：

~~~json
{
  "file_filter": {
    "mode": "common-artifacts",
    "case_sensitive": false,
    "patterns": []
  }
}
~~~

支持的模式为 `disabled`、`common-artifacts` 和 `custom-rules`。自定义规则只使用列出的
Glob 模式，不会在内置常见垃圾文件规则上追加。除非 `case_sensitive` 为 `true`，否则默认
不区分大小写。完整的匹配语法和内置规则列表请参见[创建种子](./create#文件过滤)。

命名预设可以声明自己的完整 `file_filter` 对象。未声明该成员的预设会继承完整的全局默认值；
声明后则会原子替换完整默认值，因此 `mode`、`case_sensitive` 和 `patterns` 不会分别合并：

~~~json
{
  "presets": {
    "archive": {
      "file_filter": {
        "mode": "custom-rules",
        "case_sensitive": true,
        "patterns": ["*.tmp", "cache/"]
      }
    }
  }
}
~~~

如需让预设明确关闭过滤，请使用 `mode: "disabled"`。没有 `file_filter` 的旧配置仍保持关闭
过滤的行为。Advanced GUI 页面编辑全局默认值，Create 页面显示应用预设后的最终生效策略。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--config PATH` | 指定自定义 `torrentcraft.json` 路径。 |
| `--dry-run` | 试运行：仅校验修改，不写入文件。 |
| `--backup` | 写入前自动生成 `.bak` 备份文件。 |
| `--force` | 允许 `config init` 覆盖已存在的文件。 |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
