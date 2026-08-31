# ADR 0001: Core / worker process boundary

Status: Accepted

## Decision

Keep mastering-domain code in a portable C++20 library and run heavyweight ML jobs in a separate worker process.

## Why

- a model/runtime crash must not take down the UI or future plug-in audio process
- model/provider upgrades remain replaceable
- offline work can be cancelled and restarted
- VST3 real-time safety is protected from Python/ML/runtime behavior
- cloud and local providers can share one command contract

## Consequence

The worker protocol is a versioned product interface and must receive compatibility tests before production releases.
