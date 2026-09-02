# Third-party notices

The machine-readable dependency inventory is
`third_party/dependencies.json`. Release packaging includes this file and the
available licence texts under `licenses/`.

## libsndfile 1.2.2

The packaged `sndfile.dll` provides WAV, AIFF, and FLAC streaming through the
`ICodecService` boundary. The Windows archive is pinned by SHA-256 in the
dependency inventory.

libsndfile is licensed under LGPL-2.1-or-later. A production distribution must
satisfy the applicable notice, source-availability, and relinking/replacement
obligations. Final legal approval remains a release gate.

## libebur128 1.2.6

Pinned commit `67b33abe1558160ed76ada1322329b0e9e058b02` is built statically for
BS.1770-family loudness and true-peak measurement. libebur128 is MIT licensed;
its licence text is retained in `third_party/licenses/libebur128-COPYING.txt`.

## ONNX Runtime 1.26.0

The Windows production build packages the pinned CPU runtime for isolated source-
separation worker inference. ONNX Runtime is MIT licensed. Its required upstream
copyright/licence notice must be included in final release artifacts.

## Windows Media Foundation

MP3 and AAC/M4A decode and encode use the Media Foundation components supplied
with supported Windows installations. No Media Foundation binary is redistributed
by this project. OGG/Vorbis and Opus are not claimed as standard Windows 1.0
formats.

## HTDemucs model

Model weights are downloaded on demand rather than redistributed in the package.
See `MODEL_LICENSES.md` and `models/registry.json` for the pinned identity,
declared licensing, provenance, and safety gate.

## Evaluation-only dependencies

JUCE and FFmpeg appear only in evaluation/spike or future-strategy records and
are not linked into or distributed with the Windows 1.0 application.
