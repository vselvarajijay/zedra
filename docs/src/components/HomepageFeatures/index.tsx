import type {ReactNode} from 'react';
import clsx from 'clsx';
import Heading from '@theme/Heading';
import styles from './styles.module.css';

type FeatureItem = {
  title: string;
  description: ReactNode;
};

const FeatureList: FeatureItem[] = [
  {
    title: 'Deterministic by Design',
    description: (
      <>
        Same event log and configuration always produce the identical world state
        and hash — regardless of scheduling, timing, or concurrency.
      </>
    ),
  },
  {
    title: 'Lock-Free Concurrent Ingestion',
    description: (
      <>
        Multiple producers (sensors, controllers, sim) push events simultaneously
        via a bounded lock-free MPSC queue. No producer contention.
      </>
    ),
  },
  {
    title: 'ROS 2 Ready',
    description: (
      <>
        Drop-in bridge node subscribes to <code>/zedra/inbound_events</code> and
        publishes immutable snapshot metadata at 1 kHz on <code>/zedra/snapshot_meta</code>.
      </>
    ),
  },
];

function Feature({title, description}: FeatureItem) {
  return (
    <div className={clsx('col col--4')}>
      <div className="text--center padding-horiz--md">
        <Heading as="h3">{title}</Heading>
        <p>{description}</p>
      </div>
    </div>
  );
}

export default function HomepageFeatures(): ReactNode {
  return (
    <section className={styles.features}>
      <div className="container">
        <div className="row">
          {FeatureList.map((props, idx) => (
            <Feature key={idx} {...props} />
          ))}
        </div>
      </div>
    </section>
  );
}
