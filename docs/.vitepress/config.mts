import { defineConfig } from 'vitepress'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  title: "ilias",
  description: "Modern Lightweight C++ Async Runtime",
  locales:{
    "root": {
      label: "English",
      lang: "en-US",

      themeConfig: {
        sidebar: [
          {
            collapsed: false,
            text: 'Docs',
            items: [
              { text: 'API Reference', link: '/dev/api' },
            ]
          },
          {
            collapsed: false,
            text: 'Guides',
            items: [
              { text: 'Quick Start', link: '/guides/quick-start' },
              { text: 'What is ilias', link: '/guides/what-is-ilias' }
            ]
          }
        ],
      }
    },
    "zh": {
      label: "简体中文",
      lang: "zh",
      link: "/zh/",

      themeConfig: {
        sidebar: [
          {
            collapsed: false,
            text: 'Docs',
            items: [
              { text: 'API Reference', link: 'zh/dev/api' },
            ]
          },
          {
            collapsed: false,
            text: 'Guides',
            items: [
              { text: 'Quick Start', link: 'zh/guides/quick-start' },
              { text: 'What is ilias', link: 'zh/guides/what-is-ilias' }
            ]
          }
        ],
      }
    }
  },
  themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    nav: [
      { text: 'Home', link: '/' }
    ],

    socialLinks: [
      { icon: 'github', link: 'https://github.com/BusyStudent/Ilias' }
    ],
  }
})
