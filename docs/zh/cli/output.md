# 输出与退出码 (output)

TorrentCraft 遵循规范的 Unix 设计理念，提供干净的标准流分离、可预测的结构化 JSON 数据包以及标准化的进程退出码，专为日常终端使用与自动化流水线设计。

## 标准流分离规范

- **标准输出（`stdout`）**：专门用于输出核心结果数据、文本摘要或机器可读的 JSON 内容。
- **标准错误（`stderr`）**：专门用于输出实时进度条、状态提示以及诊断日志。

这种设计使得在编写脚本时，可以通过管道或重定向轻松捕获结果，完全不会被进度条字符干扰：

```bash
# 将纯净的查看摘要保存至文本文件（进度信息不会混入）
torrentcraft inspect ./payload.torrent > summary.txt

# 将制作过程中的进度日志重定向到独立日志文件
torrentcraft create ./payload -o ./payload.torrent --progress=plain 2> progress.log
```

## 结构化 JSON 规范 (`--json`)

传入 `--json` 参数时，命令会输出规范统一的 JSON 响应数据包：

### 成功响应：
```json
{
  "ok": true,
  "data": { ... }
}
```

### 失败响应：
```json
{
  "ok": false,
  "error": {
    "code": "input_validation_error",
    "message": "详细的错误原因说明",
    "fields": { ... }
  }
}
```

如果文件系统路径中包含非 UTF-8 字节（例如在 Linux 上），序列化 JSON 时会自动将无效字节替换为 `U+FFFD`，确保 `--json` 始终输出合法的 UTF-8 JSON，不会在文件操作完成后因编码问题报错中断。

## 文件过滤报告

从目录制作种子时，无论成功写入还是使用 `--dry-run` 试运行，都会提供有效文件过滤器
排除的条目。普通文本输出包含相对路径、条目类型、匹配规则和字节摘要。匹配到的目录会合并为
一条记录，并附带后代条目数和字节数，不会重复列出后代。

使用 `--json` 时，创建结果包含 `data.filtered.count`、`data.filtered.bytes`
和 `data.filtered.entries`。空报告表示没有条目被过滤。报告只提供信息，不会删除或修改源文件。

> **关于完整性校验的说明**：在 [`verify`](./verify) 内容校验中，发现文件损坏或不匹配属于正常生成的比对报告，因此 JSON 响应中仍为 `ok: true`，但命令行退出码会设为 `6`，便于脚本和 CI 流水线精确判断。

## 进度显示模式 (`--progress`)

制作（`create`）和校验（`verify`）支持三种进度输出模式：
- `plain`：逐行输出纯文本进度（最适合 CI 构建日志）。
- `tty`：在终端中显示美观的动态进度条（当标准错误连接到终端时自动启用）。
- `json`：逐行输出 JSON 事件流（NDJSON），便于被前端或其他应用程序实时解析。

## 进程退出码速查表

TorrentCraft 的退出码分类借鉴了 HTTP 状态码的逻辑，语义明确且长期稳定：

| 退出码 | 状态 | 典型触发原因 |
| ---: | --- | --- |
| `0` | **成功 (Success)** | 操作顺利完成。 |
| `2` | **用法错误 (Usage Error)** | 参数格式错误、缺少必填参数或未知命令。 |
| `3` | **校验错误 (Validation Error)** | 文件结构校验不通过、传入无效 JSON 或格式损坏。 |
| `4` | **文件不存在 (Not Found)** | 输入文件、目标目录或配置文件不存在。 |
| `5` | **权限不足 (Permission Denied)** | 无法读取源文件或无权写入目标路径。 |
| `6` | **冲突 / 内容不匹配 (Conflict / Mismatch)** | 目标文件已存在（未加 `--overwrite`），或校验发现文件损坏。 |
| `7` | **操作取消 (Cancelled)** | 用户中断操作（Ctrl+C）或在 GUI 中取消任务。 |
| `8` | **I/O 读写失败 (I/O Error)** | 磁盘读写异常或底层文件系统故障。 |
| `9` | **资源受限 (Resource Limit)** | 内存使用超出设定的上限或工作集耗尽。 |
| `10` | **内部错误 (Internal Error)** | 未知异常或不支持的操作。 |

## 使用 Ctrl+C 取消

在运行 `create`、`verify` 或 `tracker rewrite` 期间按下 **Ctrl+C**，程序会在下一个安全点平滑停止，自动清理未完成的临时文件，并以退出码 `7` 结束。被中断的 `tracker rewrite` 会保留已经处理完的文件，并在 `--json` 输出中标记 `"cancelled": true`（且 `"ok": false`）。连续按第二次 **Ctrl+C** 则会立即强制退出。

- **Windows 终端交互**：日常命令行操作请使用 `torrentcraft.exe`。虽然 `torrentcraft-gui.exe` 也支持传入 CLI 参数，但它属于窗口子系统（GUI）程序——终端不会等待它执行完毕，也不会将 Ctrl+C 传递给它。如果确实通过 `torrentcraft-gui.exe` 启动了命令，请按 **Ctrl+Break** 取消（在启用了 PSReadLine 的 PowerShell 中，按 Ctrl+Break 后还会进入命令行调试器，输入 `c` 回车即可退出）。
- **在 Windows 脚本中调用**：在自动化脚本（如 Python `subprocess`）中请务必调用 `torrentcraft.exe`。因为 `torrentcraft-gui.exe` 会将输入输出重新绑定到控制台窗口，导致标准输出管道捕获与 `--json` 解析不可靠。若需由脚本主动取消任务，可在启动 `torrentcraft.exe` 时指定 `CREATE_NEW_PROCESS_GROUP`，随后发送 `CTRL_BREAK_EVENT` 并等待进程以退出码 `7` 结束。
