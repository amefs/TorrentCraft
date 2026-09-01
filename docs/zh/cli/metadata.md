# 编辑元数据 (metadata)

使用 `metadata` 命令查看、修改或清除已有 `.torrent` 种子中的附加元数据与身份标识字段，无需重新读取或哈希原始数据文件。

```text
torrentcraft metadata show|set|clear <torrent> ... [options]
```

所有修改都会在写入前经过严格的格式校验。推荐使用 `--dry-run` 预览修改效果，使用 `--backup` 自动保留原文件备份。

## 1. 查看元数据 (`show`)

```bash
torrentcraft metadata show ./payload.torrent
torrentcraft metadata show ./payload.torrent --json
```

可完整查看种子的注释、制作者、创建时间、Web Seed 做种源、DHT 节点、私有标记以及来源标识。

## 2. 修改与设置元数据 (`set`)

```bash
torrentcraft metadata set ./payload.torrent \
  --comment '官方正式版本' \
  --created-by 'TorrentCraft' \
  --creation-time now \
  --backup
```

### 支持修改的字段清单：

| 参数 | 说明 |
| --- | --- |
| `--comment TEXT` | 设置顶层注释说明文字。 |
| `--created-by TEXT` | 设置顶层制作者签名。 |
| `--info-source TEXT` | 设置显式的 `info["source"]` 来源扩展字段。 |
| `--name TEXT` | 修改种子的内部根名称（Info Name）。 |
| `--creation-time N\|now` | 设置创建时间戳（`now` 代表当前时间，或传入 Unix 秒数）。 |
| `--web-seed URL` | 设置 Web Seed 网络做种源。 |
| `--dht-node HOST:PORT` | 设置 DHT 引导节点。 |
| `--private` | 开启私有种子标记（Private）。 |
| `--no-private`, `--public` | 清除私有种子标记。 |

## 3. 一键清空元数据 (`clear`)

明确清空种子内的指定元数据字段：

```bash
# 一次性清空注释、制作者签名与 Web Seed
torrentcraft metadata clear ./payload.torrent \
  --comment --created-by --web-seeds \
  --backup
```

`clear` 支持清除：`--comment`、`--created-by`、`--info-source`、`--creation-time`、`--web-seeds` 和 `--dht-nodes`。

## 身份标识字段 vs. 内容结构字段

- **身份标识字段（`--name`、`--private`、`--info-source`）**：修改这些字段会更新种子的 Info Hash（身份识别码），但**无需**重新读取或哈希庞大的数据文件。
- **内容结构字段**：增删文件、修改单个文件路径或调整分块大小会破坏分块边界，属于重新制种范畴，无法直接通过元数据修改实现。请使用 [`create`](./create) 重新制作。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--dry-run` | 试运行：仅校验并预览修改，不写入文件。 |
| `--backup` | 写入前自动生成 `.bak` 备份文件。 |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
