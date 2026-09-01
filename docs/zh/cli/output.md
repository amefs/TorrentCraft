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
