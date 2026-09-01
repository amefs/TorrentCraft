import { defineConfig } from 'vitepress'

const base = process.env.VITEPRESS_BASE ?? '/'

const englishSidebar = [
  {
    text: 'Getting Started',
    collapsed: false,
    items: [
      { text: 'Overview', link: '/' },
      { text: 'Getting Started', link: '/cli/getting-started' },
      { text: 'CLI Guide', link: '/cli/' },
      { text: 'Desktop GUI', link: '/cli/gui' }
    ]
  },
  {
    text: 'Creating Torrents',
    collapsed: false,
    items: [
      { text: 'Create Torrents', link: '/cli/create' },
      { text: 'Manage Presets', link: '/cli/preset' },
      { text: 'Manage Configuration', link: '/cli/config' }
    ]
  },
  {
    text: 'Inspecting & Verifying',
    collapsed: false,
    items: [
      { text: 'Inspect Torrents', link: '/cli/inspect' },
      { text: 'Print File Tree', link: '/cli/tree' },
      { text: 'Verify Content', link: '/cli/verify' },
      { text: 'Validate Torrents', link: '/cli/validate' }
    ]
  },
  {
    text: 'Editing Torrents',
    collapsed: false,
    items: [
      { text: 'Manage Trackers', link: '/cli/tracker' },
      { text: 'Edit Metadata', link: '/cli/metadata' }
    ]
  },
  {
    text: 'Reference',
    collapsed: false,
    items: [
      { text: 'Path Inference', link: '/cli/path-inference' },
      { text: 'Output & Exit Codes', link: '/cli/output' },
      { text: 'Shell Completion', link: '/cli/completion' }
    ]
  }
]

const chineseSidebar = [
  {
    text: '快速入门',
    collapsed: false,
    items: [
      { text: '总览', link: '/zh/' },
      { text: '开始使用', link: '/zh/cli/getting-started' },
      { text: 'CLI 使用指南', link: '/zh/cli/' },
      { text: '桌面 GUI 客户端', link: '/zh/cli/gui' }
    ]
  },
  {
    text: '制作种子',
    collapsed: false,
    items: [
      { text: '创建种子', link: '/zh/cli/create' },
      { text: '管理预设模板', link: '/zh/cli/preset' },
      { text: '管理配置', link: '/zh/cli/config' }
    ]
  },
  {
    text: '查看与校验',
    collapsed: false,
    items: [
      { text: '查看种子', link: '/zh/cli/inspect' },
      { text: '打印文件树', link: '/zh/cli/tree' },
      { text: '验证内容', link: '/zh/cli/verify' },
      { text: '校验种子', link: '/zh/cli/validate' }
    ]
  },
  {
    text: '修改与维护',
    collapsed: false,
    items: [
      { text: '管理 Tracker', link: '/zh/cli/tracker' },
      { text: '编辑元数据', link: '/zh/cli/metadata' }
    ]
  },
  {
    text: '参考指南',
    collapsed: false,
    items: [
      { text: '智能路径推断', link: '/zh/cli/path-inference' },
      { text: '输出与退出码', link: '/zh/cli/output' },
      { text: 'Shell 自动补全', link: '/zh/cli/completion' }
    ]
  }
]

const englishNav = [
  { text: 'Getting Started', link: '/cli/getting-started' },
  { text: 'CLI Guide', link: '/cli/' },
  { text: 'Desktop GUI', link: '/cli/gui' }
]

const chineseNav = [
  { text: '开始使用', link: '/zh/cli/getting-started' },
  { text: 'CLI 使用指南', link: '/zh/cli/' },
  { text: '桌面 GUI', link: '/zh/cli/gui' }
]

export default defineConfig({
  base,
  title: 'TorrentCraft',
  description: 'User documentation for TorrentCraft CLI and Desktop GUI.',
  locales: {
    root: {
      label: 'English',
      lang: 'en',
      themeConfig: {
        nav: englishNav,
        sidebar: {
          '/': englishSidebar
        }
      }
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      link: '/zh/',
      themeConfig: {
        nav: chineseNav,
        sidebar: {
          '/zh/': chineseSidebar
        }
      }
    }
  },
  themeConfig: {
    search: {
      provider: 'local'
    },
    outline: 'deep',
    lastUpdated: true
  }
})
