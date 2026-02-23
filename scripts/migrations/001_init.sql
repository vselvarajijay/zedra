-- First SQL migration: initialize Zedra ClickHouse schema.
-- Creates database and zedra_events table for event telemetry and deterministic replay.

CREATE DATABASE IF NOT EXISTS zedra;

USE zedra;

CREATE TABLE IF NOT EXISTS zedra_events
(
    run_id UUID COMMENT 'Unique identifier for the robot run or simulation session',
    tick UInt64 COMMENT 'Primary logical time component',
    tie_breaker UInt32 COMMENT 'Secondary logical time component for deterministic ordering',
    ingestion_ts DateTime64(3) DEFAULT now64(3) COMMENT 'Wall-clock time when the event was received by the writer',
    event_type UInt16 COMMENT 'Identifier mapping to the specific event struct or schema',
    payload String COMMENT 'Serialized event data (raw bytes/JSON) or URI pointer to external blob storage'
)
ENGINE = MergeTree()
PARTITION BY toYYYYMMDD(ingestion_ts)
ORDER BY (run_id, tick, tie_breaker, event_type)
SETTINGS index_granularity = 8192;
