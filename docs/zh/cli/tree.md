# 打印文件树 (tree)

使用 `tree` 命令以直观的树状图展示种子内部记录的文件和目录层级结构。

```text
torrentcraft tree <torrent> [--depth N] [options]
```

## 常用示例

```bash
# 1. 打印完整的文件和目录层级树
torrentcraft tree ./payload.torrent

# 2. 仅查看根节点及一级概览
torrentcraft tree ./payload.torrent --depth 0

# 3. 将展开深度限制为 1 级子目录
torrentcraft tree ./payload.torrent --depth 1

# 4. 为脚本输出嵌套的 JSON 目录树结构
torrentcraft tree ./payload.torrent --json
```

## 深度控制与显示说明

- `--depth N`：限制目录树的展开深度（必须是非负整数）。`--depth 0` 仅显示根节点；省略该参数时会展开完整的多级目录树。
- **自动过滤 BEP 52 填充文件**：在 BitTorrent v2 规范中生成的内部 padding 对齐文件会在文本和 JSON 输出中自动隐藏，保证展示清爽干净。
- **跨平台字符兼容**：终端输出默认使用清晰美观的连接符（`├──`、`└──`）；在旧版 Windows 控制台下会自动优雅降级为 ASCII 连接符。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--depth N` | 限制输出的最大目录深度（非负整数）。 |
| `--json` | 输出嵌套结构的 JSON 目录树。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
