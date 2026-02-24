import type {SidebarsConfig} from '@docusaurus/plugin-content-docs';

const sidebars: SidebarsConfig = {
  docsSidebar: [
    'intro',
    'getting-started',
    'architecture',
    {
      type: 'category',
      label: 'Core Concepts',
      items: [
        'core-concepts/events',
        'core-concepts/world-state',
        'core-concepts/reducer',
        'core-concepts/determinism',
      ],
    },
    'cli',
    'ros2-integration',
    'docker',
    'api-reference',
  ],
};

export default sidebars;
