# 管理 Tracker (tracker)

使用 `tracker` 命令查看、添加、删除或批量替换已有 `.torrent` 种子中的 Tracker 服务器列表及分层。

```text
torrentcraft tracker list|add|remove|replace <torrent> ... [options]
```

修改默认会直接写回原种子文件。你可以使用 `--dry-run` 预览修改效果，或者使用 `--backup` 在写入前自动生成 `.bak` 备份文件。

## 1. 查看 Tracker 列表 (`list`)

```bash
# 按分层查看种子中的所有 Tracker 地址
torrentcraft tracker list ./payload.torrent

# 输出供脚本解析的 JSON 格式
torrentcraft tracker list ./payload.torrent --json
```

## 2. 添加 Tracker (`add`)

```bash
# 向 Tier 1 层级追加一个 Tracker 地址
torrentcraft tracker add ./payload.torrent \
  'https://tracker.example/announce' --tier 1
```

- 若省略 `--tier`，默认添加到 Tier 0（首选主服务器组）。
- 新建层级时只能使用下一个连续的层级索引。

## 3. 删除指定 Tracker (`remove`)

通过传入“层级索引（Tier）”和“层级内序号（Index）”（均从 0 开始）精准删除某个 Tracker：

```bash
# 删除 Tier 1 层级中的第 1 个 Tracker（序号 0）
torrentcraft tracker remove ./payload.torrent 1 0
```

> **提示**：如果不确定层级和序号，请先运行 `torrentcraft tracker list ./payload.torrent` 查看当前结构。

## 4. 全量替换 Tracker 列表 (`replace`)

一次性清空并重写种子中的全部 Tracker 列表与层级结构：

```bash
torrentcraft tracker replace ./payload.torrent \
  --tracker 'https://primary.example/announce' --tier 0 \
  --tracker 'https://backup.example/announce' --tier 1 \
  --backup
```

- Tier 层级索引范围为 `0` 到 `64`。
- 所有传入的 URL 均会在写入前严格校验 URI 格式合法性。

## 受控安全修改保障

Tracker 的增删改属于受控的安全操作（Controlled Edit）。修改只会更新种子的 announce 列表，绝不会触动分块哈希或文件目录结构。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--dry-run` | 试运行：仅校验并预览修改，不写入文件。 |
| `--backup` | 写入前自动生成 `.bak` 备份文件。 |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
