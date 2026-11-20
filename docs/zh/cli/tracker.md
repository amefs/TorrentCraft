# 管理 Tracker (tracker)

使用 `tracker` 命令查看、添加、删除或批量替换已有 `.torrent` 种子中的 Tracker 服务器列表及分层。

```text
torrentcraft tracker list|add|remove|replace <torrent> ... [options]
torrentcraft tracker rewrite <torrent|folder>... -e RULE [-e RULE]... [options]
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

## 5. 按规则改写 Tracker URL (`rewrite`)

不替换整个 Tracker 列表，而是使用 sed 风格的替换规则直接修改每个种子原有的 Tracker URL。当 PT 站点更换域名或切换 HTTPS 时，这种方式可以完好保留每个种子各自的 passkey：

```bash
# 预览文件夹及其子文件夹中所有种子的改写效果
torrentcraft tracker rewrite ./torrents -r \
  -e 's#^(https?://)tracker\.old\.org(/.*)$#\1tracker.new.org\2#' --dry-run

# 实际执行改写，并将结果输出到 ./converted（自动保持原有的子文件夹层级）
torrentcraft tracker rewrite ./torrents -r \
  -e 's#tracker\.old\.org#tracker.new.org#g' --output-dir ./converted
```

规则语法：`s<d>匹配模式<d>替换内容<d>标志`

- `<d>` 可以是除反斜杠以外的任意标点符号（如 `/`、`#`、`|`）。若模式或替换内容中包含该分隔符，请使用 `\<d>` 转义。
- `匹配模式` 采用 [RE2 正则表达式语法](https://github.com/google/re2/wiki/Syntax)，在 URL 任意位置匹配。支持普通捕获组、命名捕获组 `(?P<name>...)`、非捕获组以及 `(?i)` 标志；不支持模式内部的反向引用和前后向断言（lookaround）。
- `替换内容` 中可使用 `\0`–`\9` 或 `&` 插入完整匹配或对应捕获组，使用 `\&` 和 `\\` 表示字面 `&` 与反斜杠。
- `标志`：`g` 表示替换 URL 中的全部匹配项（默认仅替换第一处）；`i` 表示忽略大小写。
- 传入多个 `-e` 参数时，各条规则按顺序依次执行，上一条规则的输出作为下一条规则的输入；全部规则执行完毕后再统一校验最终 URL 的合法性。

批量处理行为说明：

- **输入范围**：支持传入单个或多个 `.torrent` 文件及文件夹。传入文件夹时默认只扫描顶层 `*.torrent`，加上 `-r` / `--recursive` 会递归扫描所有子文件夹（不会跟随目录符号链接）。
- **原地修改与另存输出**：未指定 `--output-dir` 时默认原地更新种子（可配合 `--backup` 保留 `.bak` 备份）；指定 `--output-dir` 时，若目标文件已存在则会报冲突错误，除非显式加上 `--force`。
- **单文件错误隔离**：若某个种子改写后的 URL 格式不合法（或原 URL 已超出长度上限），仅该种子报错并跳过写入，批次中的其他种子继续正常处理。同一层级内因改写产生的重复 URL 会自动合并（保留第一条）。没有任何 URL 发生变化的种子状态记为 `unchanged`，不会重新写入磁盘。
- **输出与退出码**：终端文本输出会逐条列出每个变更地址的 `旧 URL -> 新 URL` 并附带汇总统计；`--json` 模式输出 `data.files[]`（含 `path`、`status`、`changes[]`、`merged`、`error`）与 `data.summary`，只要有任一种子失败则 `ok` 为 `false`。全部成功（含无变化）时退出码为 `0`；规则语法或参数错误为 `2`；其余情况返回各失败文件中的最大退出码（如结果 URL 无效为 `3`，目标文件冲突为 `6`，用户中断为 `7`）。
- **Passkey 隐私保护**：规则语法报错时仅按序号指出问题规则（如 `rule[1]`），绝不在错误消息中回显规则原文，防止命令行规则中可能包含的 passkey 泄露到日志中。正常预览时则会完整展示改写后的 URL。
- **支持 Ctrl+C 安全中断**：处理过程中按 Ctrl+C 会在当前文件完成后安全停止，已写入的文件会保留，并以退出码 `7` 结束（详见[使用 Ctrl+C 取消](./output#使用-ctrl-c-取消)）。

## 受控安全修改保障

Tracker 的增删改属于受控的安全操作（Controlled Edit）。修改只会更新种子的 announce 列表，绝不会触动分块哈希或文件目录结构。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `-e`, `--expression RULE` | 添加一条 sed 风格改写规则（`rewrite`，可重复）。 |
| `-r`, `--recursive` | 包含文件夹输入的子文件夹（`rewrite`）。 |
| `--output-dir DIR` | 将改写后的种子写入 `DIR`，保持子文件夹结构（`rewrite`）。 |
| `--force` | 覆盖 `--output-dir` 中已存在的文件（`rewrite`）。 |
| `--dry-run` | 试运行：仅校验并预览修改，不写入文件。 |
| `--backup` | 写入前自动生成 `.bak` 备份文件。 |
| `--json` | 输出标准结构化 JSON 数据。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
