# OpenMoHAA

[![Build](https://github.com/GamingTux/openmohaa/actions/workflows/branches-build.yml/badge.svg?branch=main)](https://github.com/GamingTux/openmohaa/actions/workflows/branches-build.yml) [![Release](https://img.shields.io/github/v/release/GamingTux/openmohaa?include_prereleases)](https://github.com/GamingTux/openmohaa/releases) [![Downloads](https://img.shields.io/github/downloads/GamingTux/openmohaa/total)](https://github.com/GamingTux/openmohaa/releases)

![License](https://img.shields.io/github/license/openmoh/openmohaa) ![Commits](https://img.shields.io/github/commit-activity/t/openmoh/openmohaa)

![Discord](https://img.shields.io/discord/596049712579215361?logo=discord&logoColor=white&color=5865F2)

![logo](misc/openmohaa-text-sm.png)

## What is OpenMoHAA?

OpenMoHAA is an open-source project aimed at preserving and enhancing **Medal of Honor: Allied Assault** (including Spearhead and Breakthrough expansions) by providing more features and bugfixes, across modern platforms and architectures.

Powered by [ioquake3](https://github.com/ioquake/ioq3) and the [F.A.K.K SDK](https://code.idtech.space/ritual/fakk2-sdk), OpenMoHAA provides:
- Full compatibility with the original game: assets, scripts and multiplayer
- Better support for modern systems
- Cross-platform support (Linux, Windows, macOS)
- Support for both single-player and multiplayer modes
- Includes all fixes from Spearhead 2.15 and Breakthrough 2.40b
- More fixes and features, such as bots and a ban system

*OpenMoHAA is an independent project and is not affiliated with or endorsed by Electronic Arts.*

## Remaster Edition Highlights

This fork extends OpenMoHAA's OpenGL 2 renderer, audio engine, and gameplay systems with modern graphics, 3D room acoustics, and multi-core/SIMD optimizations across **x86_64, ARM64 (including Raspberry Pi 5 / Apple Silicon), Windows, Linux, and macOS** — while remaining 100% compatible with original game assets and savegames:

- **Built-In `REMASTER` Menu (`Options -> REMASTER`):** 1-click presets (`ULTRA (GL2)`, `PERFORMANCE`, `AUTO-DRS 45 FPS`, `CLASSIC GL1`) and live in-game toggles with zero external UI files required.
- **OpenGL 2 Visual Overhaul:**
  - Real-time directional sun shadows (`r_dynamicSunShadows`, `r_dynamicSunShadowsRate`)
  - Per-pixel Blinn-Phong + metallic/Fresnel model lighting (`r_modelPerPixelLighting`)
  - Animated refractive/reflective water with sky Fresnel & sun glint (`r_modernWater`)
  - Material-aware world micro-relief, specular & grazing Fresnel (`r_worldMicroRelief`)
  - GPU vertex foliage & flag wind synchronized with sun shadows (`r_foliageWind`)
  - Emissive window/lantern glow (`r_emissiveGlow`) & per-chapter cinema color grading (`r_mapAtmosphere`)
  - Modern dynamic lights (`r_modernDlights`), soft depth-faded particles (`r_softParticles`), Cinema Bloom (`r_bloom`), cubic Filmic Tonemapping (`r_filmicTonemap`), Contrast-Adaptive Sharpening (`r_sharpen`), and Dynamic Resolution Scaling (`r_dynamicResolution`)
- **OpenAL EFX 3D Room Reverb (`s_reverb 1`):** Full `ALC_EXT_EFX` (`AL_EFFECT_EAXREVERB`) implementation mapping all 26 original EAX acoustic presets.
- **Gameplay & Immersion:** Extended client draw distance (`cg_extendedFarplane`), higher model/terrain LOD (`r_extendedLod`), persistent 45s battle marks (`cg_marks_lifetime`), modern weapon feel & muzzle light (`cg_modernGunplay`), widescreen circular binoculars fix, and savegame-compatible fair AI (`g_aiFairness`).
- **Performance & Architecture:** Multi-threaded renderer front-end worker pool (`tr_threadpool`), persistent GPU static-model VAOs, zero-syscall `GL_ARB_buffer_storage` streaming ring buffer, ARM64 NEON + x86 SSE SIMD skeletal math, and TBDR on-chip `GL_EXT_shader_framebuffer_fetch` fast paths (`-DOPTIMIZE_PI5=ON` / `-DOPTIMIZE_NATIVE=ON`).

## Getting started

- 📦 [Installing OpenMoHAA](docs/markdown/01-intro/01-installation.md)
- ▶️ [How to play: Launching the game, expansions & file locations](docs/markdown/02-running/01-running.md)
- ❓ [FAQ & Troubleshooting](docs/markdown/02-running/03-faq.md)
- 🌐 [Setting up a game server](docs/markdown/02-running/02-running-server.md)

## Reporting Issues

> [!NOTE]
> OpenMoHAA hasn't hit version 1.0.0 yet. Think of it like a beta build from the golden age of LAN parties. Features are being added, bugs are getting squashed, and more things are being tweaked. Things might change, break, or get even better over time.
> 
> If that sounds like your kind of mission, gear up, frag some bots, and help level up OpenMoHAA!

If you encounter a bug or a problem, you can do one of the following:
- Submit an [issue](https://github.com/openmoh/openmohaa/issues) on GitHub (use the template).
- Join the [OpenMoHAA Discord](https://discord.gg/NYtH58R) for a quick help.

## Additional documentation

- 📖 [Documentation](https://openmoh.github.io/openmohaa)
- ⚙️ [Game settings & configuration](docs/markdown/03-configuration/01-configuration.md)
- 📝 [Code & Scripting reference](docs/markdown/04-coding/02-coding.md)
- 📜 [Contributing guidelines](CONTRIBUTING.md)

## Current state

- 🧰 [List of differences](docs/markdown/01-intro/04-differences.md)

### Single-player

The entire single-player campaign should work (Allied Assault, Spearhead and Breakthrough). If you encounter any bug, please create a new [GitHub issue](https://github.com/openmoh/openmohaa/issues) describing them.

### Multiplayer

- Almost fully stable
- All official game modes are supported, including those from Spearhead and Breakthrough:
  - Free-For-All
  - Team-Deathmatch
  - Round-based match
  - Objective match
  - Tug-of-War (Spearhead)
  - Liberation (Breakthrough)
- Popular mods like **Freeze-Tag** are supported
- Built-in bots for offline practice and for testing
  - 🔧 [Setting up bots](docs/markdown/02-running/01-running.md#Playing-with-bots)

You can host your own [OpenMoHAA server](docs/markdown/02-running/02-running-server.md#) or join others using OpenMoHAA.

## Screenshots

|                                                                                   |                                                                            |
|-----------------------------------------------------------------------------------|----------------------------------------------------------------------------|
| ![](docs/assets/images/v0.60.0-x86_64/mohdm1_1.png)                                      | ![](docs/assets/images/v0.60.0-x86_64/training_1.png)                               |
| ![](docs/assets/images/v0.60.0-x86_64/flughafen_1.png)                                   | ![](docs/assets/images/v0.60.0-x86_64/flughafen_2.png)                            |
| ![](docs/assets/images/v0.60.0-x86_64/mohdm2_1.png "Playing Freeze-Tag mode with bots")  | ![](docs/assets/images/v0.60.0-x86_64/training_3.png "Single-Player training")    |

*More screenshots [here](docs/assets/images)*

## Development & Compiling

- 💻 [Building from source](docs/markdown/04-coding/01-compiling.md)

## Third party librairies

The following third party tools and libraries are used by the project

- [Flex](https://github.com/westes/flex)
- [Bison](https://savannah.gnu.org/projects/bison/)
- [SDL](http://www.libsdl.org/)
- [OpenAL](https://www.openal.org/)
- [LibMAD](http://www.underbit.com/products/mad/)
- [cURL](https://curl.se/)
- [Libogg](https://github.com/gcp/libogg)
- [Libvorbis](https://xiph.org/vorbis/)
- [Libopus](https://opus-codec.org/)

## Resources

- 🔗 [GitHub Repository](https://github.com/openmoh/openmohaa/)
- 🌐 [MOH-DB](https://www.moh-db.com/)
- 🕹️ [333networks](https://333networks.com/)
- 📂 [ModDB](https://www.moddb.com/games/medal-of-honor-allied-assault)
- 📂 [GameBanana](https://gamebanana.com/games/720)
- 💬 [Join us on Discord](https://discord.gg/NYtH58R)
