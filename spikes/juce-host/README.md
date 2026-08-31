# JUCE 8 standalone-host feasibility spike

The Phase 0 comparison is pinned to **JUCE 8.0.15**, tag commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`.

## Result

JUCE is technically suitable for the thin standalone/plugin host because it supplies cross-platform windows, audio devices, file formats, and later VST3 generation. It must **not** own mastering-domain logic: `amt_core` remains framework-independent.

The repository also keeps the native Win32 desktop shell as a zero-dependency fallback/proof of architectural separation. iPlug2/direct VST3 remain migration options if JUCE licensing becomes incompatible with the eventual distribution model.

## Licensing gate

JUCE 8 modules are dual-licensed under AGPLv3 and the commercial JUCE licence. A public closed-source/commercial build therefore requires an explicit licensing decision before JUCE is made a production dependency. The spike is evaluation-only and JUCE source is not vendored here.

## Build

Clone/check out JUCE 8.0.15 separately, then configure this directory with `-DJUCE_SOURCE_DIR=<path>`. This spike is intentionally outside the default build.
