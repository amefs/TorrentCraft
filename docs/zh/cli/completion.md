# Shell 自动补全 (completion)

使用 `completion` 命令可一键生成适用于 Bash、Zsh 和 Fish 的自动补全脚本，大幅提升终端操作效率。

```text
torrentcraft completion bash|zsh|fish
```

## Bash

```bash
mkdir -p ~/.local/share/bash-completion/completions
torrentcraft completion bash > ~/.local/share/bash-completion/completions/torrentcraft
```

重新打开终端，或在当前终端执行 `source` 立即生效：

```bash
source ~/.local/share/bash-completion/completions/torrentcraft
```

## Zsh

```bash
mkdir -p ~/.zsh/completions
torrentcraft completion zsh > ~/.zsh/completions/_torrentcraft
```

确保 `~/.zsh/completions` 目录已包含在 `~/.zshrc` 的 `$fpath` 路径中：

```bash
fpath=(~/.zsh/completions $fpath)
autoload -U compinit && compinit
```

## Fish

```fish
mkdir -p ~/.config/fish/completions
torrentcraft completion fish > ~/.config/fish/completions/torrentcraft.fish
```

Fish 会自动加载该目录下的补全配置，无需额外配置。

---

配置完成后，输入 `torrentcraft ` 后按下 `Tab` 键即可自动补全所有子命令和常用参数。
