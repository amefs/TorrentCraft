# 验证内容 (verify)

使用 `verify` 命令比对本地文件/文件夹与 `.torrent` 种子，通过重新计算分块哈希值来校验数据是否完整无误。

```text
torrentcraft verify <torrent> <content> [options]
```

## 常用示例

```bash
# 1. 校验本地下载好的文件夹与对应种子是否一致
torrentcraft verify ./payload.torrent ./payload

# 2. 为自动化流水线输出结构化 JSON 校验报告
torrentcraft verify ./payload.torrent ./payload --json

# 3. 启用多线程并发与自定义内存缓冲区进行极速校验
torrentcraft verify ./payload.torrent ./payload \
  --verify-workers 4 \
  --verify-memory 64MiB
```

## 单文件状态与脚本自动化集成

验证过程会细致比对每个逻辑文件的完整性：
- 终端模式下，每个文件都会标明具体状态（如 *通过 (Verified)*、*不匹配 (Mismatch)* 或 *缺失 (Missing)*）。
- **自动化与退出码规范**：如果发现任何文件损坏或内容不匹配，命令在完整生成报告后会以退出码 `6` 结束；而在 `--json` 模式下，响应包仍保持 `ok: true`（代表校验任务本身已成功执行完毕），方便自动化脚本与 CI 流水线精准捕获内容异常：
  ```bash
  torrentcraft verify ./payload.torrent ./payload || echo "发现文件损坏或不完整！"
  ```

## 大容量种子展示优化与进度

- **大量文件自适应精简**：在控制台文本模式下，当种子包含超过 50 个文件时，程序会自动显示全局汇总，并只打印校验失败或缺失的文件行，避免控制台被刷屏。
- **进度反馈**：`--progress <mode>` 可定制写入标准错误的实时进度（`plain` 逐行输出、`tty` 动态终端进度条、`json` 事件流）。使用 `--quiet` 可隐藏所有非错误输出。

## 性能调优与资源控制

```bash
torrentcraft verify ./payload.torrent ./payload \
  --verify-workers 4 \
  --verify-memory 128MiB \
  --memory-working-set-limit 1GiB
```

- `--verify-workers N`：并发哈希工作线程数（默认为 `1`）。在搭载高速 NVMe 固态硬盘的多核处理器上，适当调大可显著提升校验速度。
- `--verify-memory SIZE`：用于哈希计算的内存缓存区大小（如 `64MiB`，默认 `32MiB`）。
- `--memory-working-set-limit SIZE`：限制进程最大工作集内存占用（默认 `512MiB`，主要用于 Windows）。

## 参数速查表

| 参数 | 说明 |
| --- | --- |
| `--config PATH` | 从指定配置文件读取验证参数。 |
| `--verify-workers N` | 并发哈希计算的工作线程数（默认：1）。 |
| `--verify-memory SIZE` | 校验哈希内存缓存大小（默认：32 MiB）。 |
| `--memory-working-set-limit SIZE` | 限制进程工作集内存上限（默认：512 MiB）。 |
| `--progress MODE` | 进度显示模式（`json`、`plain`、`tty`）。 |
| `--json` | 输出稳定 JSON 报告。 |
| `--quiet` | 抑制成功时的控制台文本输出。 |
| `-h`, `--help` | 显示命令帮助。 |
