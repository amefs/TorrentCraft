# 管理预设模板 (preset)

预设（Preset）是将常用的种子制作参数（如协议格式、分块大小、私有标记、文件排序策略等）保存为命名模板，存放在 `torrentcraft.json` 中，方便在制作种子时一键复用。

```text
torrentcraft preset list|show <name>|add <file>|remove <name> [options]
```

## 1. 查看预设列表与详情

```bash
# 列出所有已保存的预设名称
torrentcraft preset list
torrentcraft preset list --json

# 查看指定预设的详细参数
torrentcraft preset show release
```

## 2. 从文件导入预设 (`add`)

将预设配置保存为一个 JSON 文件（如 `preset_release.json`）：

```json
{
  "format": "hybrid",
  "piece_size": 4096,
  "private": true,
  "file_order": "natural"
}
```

导入到配置文件中：

```bash
torrentcraft preset add ./preset_release.json
```

- 预设名称会自动从文件名中提取并去除开头的 `preset_`（例如 `preset_release.json` 自动命名为 `release`）。
- 若预设名称已存在，需显式加上 `--force` 进行覆盖。

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
命令行参数 > 选中的预设模板 > 全局配置默认值 > 内置引擎默认值
```

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--config PATH` | 指定自定义 `torrentcraft.json` 路径。 |
| `--force` | 导入时允许覆盖同名的已有预设。 |
| `--dry-run` | 试运行：仅校验修改，不写入文件。 |
| `--backup` | 写入前自动生成 `.bak` 备份文件。 |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
