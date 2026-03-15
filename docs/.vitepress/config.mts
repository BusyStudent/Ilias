import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'ilias',
  description: 'Modern Lightweight C++ Async Runtime',
  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      themeConfig: {
        sidebar: [
          {
            collapsed: false,
            text: 'Docs',
            items: [
              { text: 'API Reference', link: '/dev/api' },
            ],
          },
          {
            collapsed: false,
            text: 'Guides',
            items: [
              { text: 'Quick Start', link: '/guides/quick-start' },
              { text: 'What is ilias', link: '/guides/what-is-ilias' },
            ],
          },
        ],
      },
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      link: '/zh/',
      themeConfig: {
        sidebar: [
          {
            collapsed: false,
            text: '开发参考',
            items: [
              { text: 'API 概览', link: '/zh/dev/api' },
              { text: 'Agent 参考手册', link: '/zh/dev/agent-reference' },
            ],
          },
          {
            collapsed: false,
            text: '使用指南',
            items: [
              { text: '快速开始', link: '/zh/guides/quick-start' },
              { text: '什么是 ilias', link: '/zh/guides/what-is-ilias' },
              { text: '使用 Ilias 构建异步软件', link: '/zh/guides/build-async-software' },
            ],
          },
        ],
      },
    },
  },
  themeConfig: {
    nav: [{ text: 'Home', link: '/' }],
    socialLinks: [{ icon: 'github', link: 'https://github.com/BusyStudent/Ilias' }],
  },
})
