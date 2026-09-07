# 智能路径推断

TorrentCraft 支持智能路径推断：当你在命令行中直接传入文件或文件夹路径而不是子命令名称时，程序会自动识别你的意图并执行对应操作。

## 推断规则速查表

| 输入参数 | 自动推断执行的操作 |
| --- | --- |
| **单个 `.torrent` 文件路径** | 自动执行 [`inspect`](./inspect)（查看种子信息）。 |
| **单个普通文件或文件夹路径** | 自动执行 [`create`](./create)（在旁边创建同名的 `<name>.torrent`）。 |
| **1 个种子文件 + 1 个数据文件/文件夹** | 自动执行 [`verify`](./verify)（校验本地数据完整性）。 |
| **1 个目标目录 + 1 个普通文件** | 自动在目标目录中为该文件创建种子。 |
| **两个种子文件或产生歧义的组合** | 严格报错并退出，绝不盲目猜测。 |

## 实际使用示例

```bash
# 1. 快速查看种子信息（等价于：torrentcraft inspect ./payload.torrent）
torrentcraft ./payload.torrent

# 2. 快速从文件夹制作种子（等价于：torrentcraft create ./payload -o ./payload.torrent）
torrentcraft ./payload

# 3. 快速完整性校验（种子与数据目录前后顺序随意）
torrentcraft ./payload.torrent ./payload
torrentcraft ./payload ./payload.torrent --json
```

## 参数与配置

完成目标推断后，剩余参数会交给与显式子命令相同的命令处理器。`--config`、`--preset`、
`--preset-file`、`--format`、`--piece-size`、`--file-order`、文件过滤器、Tracker、进度和
资源限制等 create 参数都可以放在路径前或路径后：

```bash
torrentcraft --config ./torrentcraft.json --preset release ./payload --dry-run --json
torrentcraft ./payload --format v1 --piece-size 1024 --output ./release.torrent
```

因此，路径推断 create 与 `torrentcraft create` 使用相同的 `default_preset` 和 overlay 优先级：
先读取 `config.defaults`，没有显式预设时再叠加 `default_preset`，最后应用显式 CLI 参数。
配置发现规则也保持一致：显式 `--config` 优先，否则依次检查当前工作目录、可执行文件目录和用户配置目录；
不会自动搜索输入路径所在的目录。完整解析图请参见[制作设置的解析顺序](./config#制作设置的解析顺序)。

create 仅在没有提供 `-o`/`--output` 时补充自动生成的 `<输入路径>.torrent` 目标。显式输出始终
优先，包括“目标目录 + 普通文件”的推断形式。

## 优先级与防冲突说明

- **命令关键字优先**：内置的子命令名称（如 `create`、`inspect`、`verify`、`config` 等）始终优先于同名文件。如果当前目录下刚好有一个名为 `create` 的文件夹，请使用显式子命令。
- **何时使用显式命令**：当路径组合有歧义，或需要查看某个具体子命令的帮助时使用显式命令；其他情况下，智能推断与目标子命令使用相同的参数和配置优先级。
