# 校验种子 (validate)

使用 `validate` 命令快速检查 `.torrent` 种子文件本身的语法、Bencode 编码、必要字段及哈希长度等规范合规性，无需读取或下载实际数据文件。

```text
torrentcraft validate <torrent> [options]
```

## 常用示例

```bash
# 1. 对种子文件执行标准语法与格式校验
torrentcraft validate ./payload.torrent

# 2. 启用严格模式进行规范质检，并输出 JSON 数据
torrentcraft validate ./payload.torrent --strict --json
```

## 严格模式与兼容模式

- **标准模式（默认）**：检查种子核心结构是否合法，同时允许现实中常见的一些非致命兼容容错（如非标准附加字段或特殊字符集）。
- **严格模式（`--strict`）**：严苛校验规范合规性，只要种子依赖宽松容错规则加载便判定失败。非常适合在自动化发布流水线中用作品质质检。

> **Validate vs. Verify 概念辨析**：
> - **`validate`**（校验）：秒级检查 `.torrent` 种子文件自身的格式与规范合法性，不读取本地数据。
> - **[`verify`](./verify)**（验证）：逐块计算并比对本地数据文件的哈希完整性。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--strict` | 严格模式：拒绝只能通过容错兼容模式加载的非标种子。 |
| `--json` | 输出标准结构化 JSON 响应。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
