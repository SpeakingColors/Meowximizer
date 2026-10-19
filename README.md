# Meowximizer 🐈

<img src="assets/icon.svg" width="96" align="right" alt="icon">

Free, open source sonic enhancer, bass enhancer and exciter for Linux (VST3 and standalone). Three phase-aligned bands: low contour and bass harmonics, mid and high time offsets and dynamic lift, air exciter, optional OTT-style squeeze, light saturation and an output limiter.

## Status
Early scaffold. It builds in CI (Linux VST3 and standalone) but has not been tuned by ear yet. Free and open source. Donations welcome: https://ko-fi.com/studio87

## Docs
- [Roadmap](ROADMAP.md)

## Build
CMake 3.22+, a C++17 compiler and the usual JUCE Linux libraries. `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`. JUCE and the shared cat-ui look are fetched automatically.

## License
AGPL-3.0-or-later, see [LICENSE](LICENSE).

Original design. The idea of lows and mids arriving slightly after the highs is a long-known public concept (sonic maximizers); all numbers here are our own choices. It is not affiliated with BBE and contains nothing from their products.
