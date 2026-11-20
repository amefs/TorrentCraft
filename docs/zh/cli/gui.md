# 桌面 GUI 客户端

TorrentCraft 包含基于 Qt 开发的现代化图形界面桌面程序 `torrentcraft-gui`。它与 CLI 命令行工具共享相同的底层核心逻辑与统一的配置文件（`torrentcraft.json`），提供直观便捷的可视化操作体验。

## 启动程序

```bash
torrentcraft-gui
```

- **Linux**：发布版 GUI 打包的是 Qt 的 X11/XCB 图形后端（不包含原生 Wayland 后端）。无参数启动时，程序会检查 `DISPLAY` 环境变量是否已设置以及能否连接到 X11/XWayland 显示服务；若无法连接，则向标准错误输出提示并直接打印 CLI 用法说明，避免在启动 Qt 时崩溃报错。因此，在 Wayland 桌面环境下请确保已启用 XWayland（仅设置 `WAYLAND_DISPLAY` 不足以启动该构建）。注意：此项显示检查不涵盖系统缺少 `xcb-cursor0` 等动态库的情况。
- **Windows**：采用静态 `/MT` 编译打包，无需安装额外的运行库或依赖环境，开箱即用。虽然给 `torrentcraft-gui.exe` 传入命令行参数也能在当前控制台执行 CLI 命令，但 Windows 终端不会等待 GUI 程序结束，且会直接拦截 Ctrl+C。日常命令行或脚本调用请使用 `torrentcraft.exe`；若确实通过 `torrentcraft-gui.exe` 启动了命令，请按 **Ctrl+Break** 取消（参见[使用 Ctrl+C 取消](./output#使用-ctrl-c-取消)）。

---

## 1. 制作种子 (Create)

**Create** 页面是制作 `.torrent` 种子文件的主要入口，支持选择单个文件或整个文件夹进行打包制作。

![Create 页面](/screenshots/Create.png)

### 核心功能与设置：
- **输入与输出**：选择要打包的源文件/文件夹以及种子文件的保存路径；若目标文件已存在，可勾选允许覆盖。
- **协议与分块设置（Settings）**：
  - **格式（Format）**：支持选择 **V1**（兼容所有旧款客户端）、**V2**（新一代 BitTorrent v2 规范，基于 SHA-256 哈希树）或 **Hybrid**（混合格式，推荐选用，兼顾所有新老客户端）。
  - **分片大小（Piece Size）**：支持**自动计算（Automatic）**，或通过下拉菜单选择 16 KiB 到 16 MiB 之间的 2 的幂。Advanced 的制作默认值使用相同下拉菜单，预设可以覆盖默认值；配置统一以 KiB 保存（8 MiB 对应 8192 KiB）。
  - **文件排序（File Order）**：支持字典序（Lexicographical）、自然排序（Natural）、规范对齐（Canonical Alignment）或广度优先（Breadth-First）。
  - **文件过滤（File Filter）**：支持**关闭**（Disabled）、**常见系统垃圾**（Common system artifacts）或
    **自定义排除规则**（Custom exclusion rules）。自定义规则每行使用一个 Glob 模式，并替换内置常见垃圾规则；
    **区分大小写**（Case-sensitive）复选框控制匹配策略。
  - **私有种子（Private）**：勾选后生成私有种子（Private Torrent），自动禁用公网 DHT、PEX 及本地节点发现，专为 PT 站点设计。
- **元数据与 Tracker（Fields）**：支持配置分层 Tracker 服务器列表、HTTP/HTTPS Web Seed（网络做种源）、种子注释、制作者签名以及自定义来源标识（Source）。
- **过滤报告（Filter report）**：目录制作或试运行完成后，可查看最终生效过滤器排除的相对路径和字节摘要。
- **制作者覆盖**：Create 页面显示当前生效的制作者值。勾选 **Use custom creator** 后，
  可以临时使用当前 preset 的值；如果 preset 没有该字段，则从 config 默认值或内置的
  `TorrentCraft` 开始。取消勾选会跳过 preset 中的制作者字段，回退到 config 或内部默认值。
- **试运行与进度监控（Dry Run）**：支持仅预检配置而不实际生成文件；开始制作后支持实时进度条显示与一键取消操作。
- **一键重置（Reset / 重置）**：清空当前页面的输入输出路径与手动修改，同时保留当前选中的预设并重新应用其设置。

> **预设自动填充**：页面上的各项参数会自动读取当前生效的预设模板和全局默认值。在页面上手动修改的参数仅对当前制作生效，不会意外覆盖已保存的预设模板。

Advanced 页面的 **Default preset** 保存在 CLI 与 GUI 共享的顶层 `default_preset` 配置项中。
Create 未显式选择预设时，两个前端都会先读取 `config.defaults`，再将该默认预设叠加在其上。
显式选择的预设会替换 `default_preset`；之后 CLI 参数和 GUI 表单修改分别应用在各自的最终层。
完整的共享解析链路请参见[制作设置的解析顺序](./config#制作设置的解析顺序)。

### 制作者覆盖

制作者复选框是独立于普通字段优先级的特殊行为：

~~~mermaid
flowchart TD
    CreatorToggle["Use custom creator 复选框"] --> Checked{"是否勾选？"}
    Checked -- "否" --> ConfigCreator["config.defaults.created_by"]
    Checked -- "是" --> PresetCreator["当前预设的 created_by"]
    PresetCreator --> HasPresetCreator{"预设是否提供该值？"}
    HasPresetCreator -- "是" --> CreatorResult["使用预设制作者"]
    HasPresetCreator -- "否" --> ConfigCreator
    ConfigCreator --> HasConfigCreator{"配置是否提供该值？"}
    HasConfigCreator -- "是" --> CreatorResult
    HasConfigCreator -- "否" --> BuiltInCreator["内置 TorrentCraft 制作者"]
~~~

取消勾选时会明确跳过当前预设中的 `created_by`。勾选后，当前预设可以提供临时制作者覆盖；
如果该值缺失，仍会依次回退到配置默认值和内置值。

---

## 2. 安全修改元数据 (Modify)

**Modify** 页面用于直接打开现有的 `.torrent` 种子文件，在无需重新读取或哈希原始数据文件的前提下，安全修改种子的元数据信息。

![Modify 页面](/screenshots/Modify.png)

### 支持修改的内容：
- 种子名称（Name）、注释说明（Comment）、制作者签名（Created By）与创建时间戳。
- Tracker 分层列表、Web Seed 下载源、DHT 引导节点以及私有种子标记（Private）。
- 自定义 `info["source"]` 来源标识。

### 安全保护与一键清除：
- **按需单项清除**：提供专用的复选框，可明确一键清空指定的元数据字段（如清空 Tracker 列表或清空注释）。
- **严格模式（Strict）**：加载前勾选 **严格模式**（英文界面为 **Strict**），即可拒绝任何依赖宽松兼容规则才能解析的非标准种子，行为与 CLI 的 [`--strict` 参数](./validate#严格模式与兼容模式)完全一致。
- **输入输出路径分离**：默认采用不同的输出路径，防止不小心覆盖原始种子文件。
- **试运行、备份与重置（Dry Run / Backup / Reset）**：支持修改前预览变更、自动生成 `.bak` 备份，或点击 **重置** 卸载当前种子并将页面恢复为初始状态。

> [!NOTE]
> 如果修改涉及到文件增删、目录重命名或分块哈希等结构性变更，属于重新制作范畴，无法通过元数据修改实现，请使用 Create 页面重新创建。

---

## 3. 查看种子与文件树 (Inspect)

**Inspect** 页面用于秒级解析种子文件本身，无需本地拥有对应的数据文件即可全方位查看种子的各项详细参数。

![Inspect 页面](/screenshots/Inspect.png)

### 查看内容：
- **基础摘要**：显示种子名称、协议格式（V1 / V2 / Hybrid）、私有状态、总分块数、以易读 IEC 单位显示的分片大小（例如 8 MiB）、数据总容量、制作者信息以及 Info Hash 校验码（同时展示 SHA-1 与 SHA-256）。
- **层级文件树**：直观展示种子内包含的完整目录层级与文件列表，支持展开/折叠并显示各文件大小。对于多文件（目录）种子，文件树会以种子的根文件夹作为顶层节点，与 `torrentcraft tree` 命令行输出保持一致。
- **Tracker 与元数据列表**：清晰列出种子配置的所有 Tracker 分层、Web Seed 列表及原始键值信息。
- **诊断警告、严格模式与重置**：自动检测非标字段、老旧编码或潜在兼容性问题。勾选 **严格模式**（**Strict**）可拒绝需要兼容容错才能加载的种子（详见[严格模式与兼容模式](./validate#严格模式与兼容模式)）；点击 **重置**（**Reset**）可清空当前已加载的种子。

---

## 4. 本地内容完整性校验 (Verify)

**Verify** 页面用于将本地下载或存储的文件/文件夹与 `.torrent` 种子进行比对，重新计算哈希值以检测文件是否存在损坏、缺失或内容不匹配。

![Verify 页面](/screenshots/Verify.png)

### 校验功能亮点：
- **根据种子结构自动切换选择器**：加载种子后，本地内容选择入口会自动匹配种子的文件结构——单文件种子会打开“选择文件”对话框，目录种子则会打开“选择文件夹”对话框（即使该文件夹里只有一个文件）。未加载种子前两种方式均可选择。如果你为单文件种子传入了文件夹，或为目录种子传入了单个文件，程序会直接提示类型不匹配并拒绝执行，绝不会擅自猜测或改写路径。
- **文件级状态展示**：展开文件树（目录种子同样以根文件夹作为顶层节点），即可直观查看每个文件的校验结果标签（*已通过*、*不匹配*、*文件缺失*、*等待中*）。
- **资源与并发控制**：校验过程严格遵循在**高级设置（Advanced）**中配置的并发工作线程数、内存缓冲区以及 I/O 模式。
- **安全防误触与重置**：校验运行期间若尝试关闭窗口或取消任务，会弹出确认提示，且中断后已有的诊断结果仍保留在界面上方便排查。空闲时点击 **重置**（**Reset**）即可清空当前页面。
- **哈希取消与性能**：取消在分片边界协作完成；启用取消能力不会强制切换到较慢的标准 I/O，配置的 mmap/默认路径仍可使用。

---

## 5. Tracker 批量处理 (Tracker)

**Tracker** 页面专为批量维护设计，支持一次性对整个文件夹内的所有 `.torrent` 文件批量修改 Tracker 列表及层级。

![Tracker 页面（替换列表模式）](/screenshots/TrackerReplace.png)

### 使用步骤：
1. 选择存放待处理种子的**输入目录**和保存结果的**输出目录**。勾选 **包含子文件夹**（**Include subfolders**）可一并处理所有子目录中的种子；若同时指定了输出目录，会自动按原样还原对应的子目录层级。
2. 选择处理模式：**替换列表**（用下方编辑好的新列表覆盖每个种子的 Tracker）或 **改写（查找替换）**（在每个种子原有的 Tracker URL 上按规则查找替换）。
3. 点击 **重新载入文件夹（Reload Folder）** 扫描种子列表，确认无误后点击 **批量转换（Batch Convert）** 开始处理；或点击 **重置（Reset）** 清空列表并将页面恢复为初始状态。
4. 勾选 **试运行（Dry run）** 可预检变更，勾选 **备份（Backup）** 可自动保留原文件副本。

### 改写（查找替换）模式

当站点更换 Tracker 域名或协议、而你希望保留每个种子各自的私有 passkey 时，请选择 **改写（查找替换）**（**Rewrite (find and replace)**）模式。每条规则的用法与代码编辑器中的查找替换完全一致：

![Tracker 页面（改写模式）](/screenshots/TrackerRewrite.png)

- 在 **查找（Find）** 中输入原文本，在 **替换（Replace）** 中输入新文本，每个 Tracker URL 中的所有匹配项都会被替换。例如查找 `tracker.old.example`、替换为 `tracker.new.example`，即可完成域名迁移并完整保留 URL 其余部分（包括 passkey）。
- **Aa**（区分大小写）默认开启，避免误改动区分大小写的 passkey。
- **.\***（使用正则表达式）开启后，查找内容按 [RE2 正则表达式](https://github.com/google/re2/wiki/Syntax)匹配；替换框中可使用 `$1`–`$9` 引用捕获分组，`$0` 或 `$&` 引用完整匹配文本，`$$` 表示字面 `$` 字符（例如查找 `^udp://(tracker\.example):6969`、替换为 `https://$1`）。
- 点击 **+ 添加规则（+ Add rule）** 可添加多条规则，执行时从上到下依次生效；查找框为空的行会自动忽略，点击 **×** 可删除该行。（命令行下请使用 [`tracker rewrite`](tracker.md) 的 `-e 's#old#new#g'` 参数。）
- **实时预览**：点击列表中的任意种子，下方会实时展示每个地址的**原始 URL**、**改写结果**与**状态**（*已更改*、*未更改*、*已合并*，或*无效*及具体原因），并随你的输入即时刷新。点击 **全部预览（Preview all）** 可一次性检查列表中的所有种子，并在表格“状态”列标记 *将更改*、*无变化* 或 *将失败*。
- **安全保护**：当任一规则存在语法错误时，**批量转换** 按钮会自动禁用并指出有问题的规则编号。由于改写规则中可能包含私密的 passkey，规则仅保留在当前会话内存中，不会写入配置文件。

---

## 6. 高级设置与配置中心 (Advanced)

**Advanced** 页面是图形界面的全局控制中心，用于集中管理统一配置文件、制作默认值、性能参数、界面主题及运行日志。

![Advanced 页面](/screenshots/Advanced.png)

### 核心设置分区：

- **配置文件管理（Configuration File）**：
  - 显示当前正在生效的 `torrentcraft.json` 路径。
  - 支持 **浏览（Browse）** 切换配置、**重新加载（Reload）** 外部修改、**初始化（Initialize）** 生成新配置模板以及 **查看（Show）** 原始配置内容。
- **默认保存路径（Default Save Location）**：
  - 可设定新制作种子时的默认保存位置：*当前工作目录*、*最近使用目录* 或 *指定固定目录*。
- **制作默认值（Creation Defaults）**：
  - 设置制作种子时的全局默认选项（如默认协议格式、自动或固定分片大小、文件排序、私有标记、文件过滤、默认 Tracker 列表、默认制作者签名等）。
  - 全局**文件过滤（File Filter）**默认值会在当前预设没有声明过滤对象时生效。
  - 可指定 GUI 启动时默认加载的**预设模板（Default Preset）**。
- **性能与资源控制（Performance & I/O）**：
  - 调整 **磁盘 I/O 模式**（`mmap` 内存映射或标准 I/O）、**校验工作线程数**、**校验内存缓存（MiB）** 以及 **Windows 进程工作集内存上限**。
- **外观与日志诊断（Appearance & Diagnostics）**：
  - 自由切换界面样式、字体，可选择是否在文件树中显示 BEP 52 填充文件。
  - 配置日志记录级别（Debug、Info、Warning、Error）、自定义日志保存路径，并支持一键复制诊断日志供排错。

---

## 预设模板（Preset）管理

通过预设机制，你可以把不同场景下的制作参数（例如“PT 私有种子预设”、“公网开源分发预设”）保存为命名模板，存入 `torrentcraft.json`。

![Preset 预设管理](/screenshots/Preset.png)

在顶部菜单栏的 **Preset**（**预设**）菜单中支持以下操作：
- **Import From File（从文件导入）**：支持从外部 `.json` 预设文件或现有的 `.torrent` 种子文件导入命名预设。选择 `.torrent` 文件时，会弹出 **从种子导入（Import from torrent）** 窗口，供你设置预设名称并勾选要提取的可复用字段（Tracker 列表、分片大小、私有标记、来源标识、Web Seed、格式、注释或制作者）。
- **Load Preset（加载预设）**：将已保存的预设参数一键应用到当前 Create 页面。
- **Save Preset（保存预设）**：将当前 Create 页面的设置保存为新的命名预设。
- **Delete Preset（删除预设）**：从配置文件中删除指定的预设。

![从种子导入预设对话框](/screenshots/ImportFromTorrent.png)

### 参数生效优先级

在制作种子时，各项参数的最终生效顺序为：

```text
Create 页面当前手动修改 > 当前选中的 Preset（无显式选择时为 `default_preset`） > 全局配置默认值 > 内置引擎默认值
```

---

## 更多特性

- **拖拽支持（Drag & Drop）**：可直接将本地文件、文件夹或 `.torrent` 种子文件拖拽至主窗口或路径输入框中自动填入。
- **多语言即时切换**：随时可在顶部 **Language** 菜单中切换“English”与“简体中文”。
- **系统主题自适应**：在 Linux 下会自动继承桌面 GTK 主题、系统字体和文件图标；若系统无法提供图标，则自动优雅降级为内置的高清 SVG 图标。
