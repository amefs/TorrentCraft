# 管理预设模板 (preset)

预设（Preset）是将常用的种子制作参数（如协议格式、分块大小、私有标记、文件排序策略和文件过滤等）保存为命名模板，存放在 `torrentcraft.json` 中，方便在制作种子时一键复用。

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
| `--dry-run` | 试运行：仅校验修改，不写入文件。 |
| `--backup` | 写入前自动生成 `.bak` 备份文件。 |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
