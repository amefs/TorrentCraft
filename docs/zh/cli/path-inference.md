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

## 优先级与防冲突说明

- **命令关键字优先**：内置的子命令名称（如 `create`、`inspect`、`verify`、`config` 等）始终优先于同名文件。如果当前目录下刚好有一个名为 `create` 的文件夹，请使用显式子命令。
- **何时使用显式命令**：智能推断保持谨慎与极简。如果需要精细控制分块大小、协议格式、Tracker 服务器、内存限制或配置文件路径，请使用完整的显式命令。
