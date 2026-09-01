# 查看种子 (inspect)

使用 `inspect` 命令以只读方式快速解析 `.torrent` 种子文件，无需本地拥有或下载对应的数据文件。

```text
torrentcraft inspect <torrent> [options]
```

## 常用示例

```bash
# 1. 查看种子信息并在终端打印清晰的摘要
torrentcraft inspect ./payload.torrent

# 2. 输出供脚本解析的详细结构化 JSON 数据
torrentcraft inspect ./payload.torrent --json
```

## 输出信息详解

控制台摘要包含以下关键信息：
- **基础信息**：种子名称、协议格式（`v1`、`v2`、`hybrid`）、私有种子状态（Private）、总数据体积、总分块数、分块大小、制作者签名及创建时间。
- **Info Hash 校验码**：同时展示 V1 SHA-1（16 进制与 Base32）与 V2 SHA-256（16 进制）根哈希。
- **Tracker 与 Web Seed**：按分层分组展示所有 Tracker 服务器地址及 HTTP/HTTPS 做种源。
- **校验能力评估**：分析该种子支持使用哪些协议与哈希算法进行本地文件完整性校验。
- **诊断与规范警告**：自动检测非标字段、字符编码异常或不符合规范的结构性问题并给出提示。

## 脚本调用与 JSON 格式

添加 `--json` 参数时，`inspect` 会输出标准的响应数据包：

```json
{
  "ok": true,
  "data": {
    "name": "my-payload",
    "format": "hybrid",
    "private": false,
    "total_size": 104857600,
    "piece_length": 1048576,
    "piece_count": 100,
    "info_hash_v1": "...",
    "info_hash_v2": "...",
    "trackers": [
      { "tier": 0, "url": "https://tracker.example/announce" }
    ]
  }
}
```

> **Inspect vs. Verify vs. Validate 概念辨析**：
> - **`inspect`**（查看）：仅秒级解析 `.torrent` 种子本身的元数据与参数，不需要本地数据文件。
> - **[`verify`](./verify)**（验证）：对比本地文件与种子分块哈希，校验文件内容是否完整无损。
> - **[`validate`](./validate)**（校验）：快速检测 `.torrent` 文件语法与数据结构是否合规。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
