# ADR 0005: Keep the mastering core independent of the desktop/plugin host

Status: Accepted

## Decision

JUCE 8.0.15 is technically feasible and is the preferred host candidate if its commercial/licensing terms fit the release model. It will not be a dependency of `amt_core`.

The default Phase 0 executable remains a minimal native Windows shell so the repository can build without JUCE and so framework independence is continuously demonstrated.

## Rationale

JUCE can later reduce standalone/VST3 duplication, but its licensing is architecture-relevant. Keeping the host thin preserves the ability to replace it with iPlug2/direct VST3/platform UI without rewriting the mastering system.
