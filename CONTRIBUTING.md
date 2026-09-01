# Contributing to TorrentUtils

## 分支

- 从最新 `main` 创建短生命周期分支。
- 使用 `feature/<issue>-<slug>`、`fix/<issue>-<slug>`、`docs/<issue>-<slug>`、`chore/<issue>-<slug>` 或 `hotfix/<issue>-<slug>`。
- 一个分支只处理一个 issue；合并后删除该分支。

## 提交

- 使用 Conventional Commits，并在 scope 中标识受影响模块。
- 保持提交小而可审，不混合功能、重排和格式化。

## Pull Request

- 所有变更通过 Pull Request 进入受保护的 `main`；禁止直接推送。
- 合并前 rebase 到最新 `main`，所有必需的 GitHub Actions 检查必须成功。
- 至少一名非作者批准后，由维护者合并 Pull Request。

## 发布

- 稳定发布直接从已合并的 `main` 提交创建注释 `vX.Y.Z` tag。
- tag workflow 完成构建、安装消费、SBOM 和校验和后才发布。

## 紧急修复

- 从最新 `main` 创建 `hotfix/` 分支，并保持一个 issue 对应一个修复。
- 必须通过一名维护者审批以及格式、Linux Core、受影响测试和 Windows 编译门禁；合并后创建补丁 tag。
