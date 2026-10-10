// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// https://astro.build/config
export default defineConfig({
	integrations: [
		starlight({
			title: 'CoAsio',
			description: 'A C++23 coroutine runtime built on Asio, with a separate scheduler for coroutine execution.',

			defaultLocale: 'root',
			locales: {
				root: {
					label: 'English',
					lang: 'en',
				},
			},

			social: [{ icon: 'github', label: 'GitHub', href: 'https://github.com/coasio/coasio' }],

			editLink: {
				baseUrl: 'https://github.com/coasio/coasio/edit/main/docs/',
			},

			customCss: ['./src/styles/custom.css'],

			sidebar: [
				{
          label: 'Start here',
          items: [
            'getting-started',
            'getting-started/installation',
            'getting-started/first-task',
          ],
        },
				{
          label: 'Core concepts',
          items: [
            'concepts/tasks',
            'concepts/awaiting-and-spawning',
            'concepts/scheduling',
            'concepts/lifetimes',
          ],
        },
				{
          label: 'Guides',
					items: [{ autogenerate: { directory: 'guides' } }],
        },
				{
          label: 'Reference',
          items: [
            'reference',
            'reference/errors',
            'reference/cancellation',
            'reference/networking',
            'reference/filesystem',
          ],
        },
				{
          label: 'Internals',
					items: [{ autogenerate: { directory: 'internals' } }],
        },
			],
		}),
	],
});
