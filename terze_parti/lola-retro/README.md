# Lola Retro for Amiberry

The Commodore 1084S preset of [Lola Retro](https://github.com/zoran123456/lola-retro) for the Amiga
emulator [Amiberry](https://amiberry.com). It is the RetroArch shader converted to GLSL, and it gives the
same picture: the monitor as its service manual describes it, NTSC lines further apart than PAL lines,
and the picture nearly filling the glass, as owners set up the real monitor.

![Cannon Fodder and Super Stardust AGA in Amiberry with the 1084S preset](https://raw.githubusercontent.com/zoran123456/lola-retro/main/docs/amiberry-1084s.jpg)

## Install

1. Amiberry 8.3 or newer, the normal OpenGL build (macOS, Linux, Windows).
2. Copy this `lola-retro` folder into Amiberry's shader folder. **Paths → Shaders** shows where it is.
3. **Play → Display defaults → Shader** (or **Filter → Native Display Shader**): choose
   `lola-retro/amiga-1084s.glslp`.

## Amiberry settings

The defaults are right. What matters:

| Setting | Use | Why |
|---|---|---|
| Display → Line mode | **Double** (default) | Amiberry draws every line twice (720×568 for a 256-line PAL game). The shader finds the pairs and draws them as the real lines, with the gaps between them. *Scanlines* and the *fields* line modes put black or alternating rows in, which the shader would draw as real lines. |
| Display → Interlaced line mode | **Double, fields** (default) or **Double, frames** | The two fields of an interlaced screen (Workbench in laced mode) arrive woven together, and the shader draws them half a line apart, as the monitor did. *Double, fields+* blanks every other row. |
| Play → AutoCrop | off | The real monitor showed the whole picture with a border. A cropped frame still comes out at the right size, just smaller in the window. |
| Correct aspect ratio | on (default) | The picture area is then 4:3, like the tube. (The tube stays 4:3 either way.) |
| Shader for RTG modes | none | RTG screens are graphics card modes; a 15 kHz monitor could not show them. |

**Overscan** in the shader parameters is the monitor's width and height: 0.1 leaves about 5 mm of black
around a 320×256 screen, −0.015 is the factory setting with its wide black borders; **Overscan height**
adds to it for the height alone. To see the monitor at its real size on a large display instead, enter your screen's
height under **Real size** (in cm, or in inches with unit 1), in full screen. **Around the tube** then
puts the monitor around it, as large as the real one: in a room (1), or in front of one of ten moving
scenes (2–11: space, synthwave, tiles, bouncing lines, underwater, copper bars, Amiga demo, hills, night city, arcade
carpet; **Scene speed** slows them down or stops them). **Room light**
sets how bright the room is: 1 a dim room, 3 a lit room, 20 daylight, and **Screen light on the casing**
how strongly the screen lights the walls around the glass (1 as on the real monitor).

## Parameters

**Filter → Shader Parameters...** lists 35 of them, each once. They mean the same as in RetroArch
(see the [shader pack's README](https://github.com/zoran123456/lola-retro/blob/main/shaders/lola-retro/README.md)); **Reset to Shader Defaults** returns to
the 1084S. Fixed in this version: the cable (RGB), the console timing (Amiga) and the tube described in
millimetres.

## Differences from the RetroArch version

- Amiberry draws every line twice. Every frame the shader compares the two rows of each line: identical
  rows are one line, rows that differ are the two fields of an interlaced screen.
- No phosphor afterglow: Amiberry has no feedback textures. A still picture is the same; moving objects
  leave no faint trail.
- The tube stays 4:3 whatever shape Amiberry's picture area has.

Checked against Amiberry's source with a test renderer that runs the preset the way Amiberry does
(`tools/amiberry-preview.py`): on the same games the picture matches the RetroArch version, with the
brightness within 1%. In use in Amiberry on macOS; reports from other systems are welcome.

## For developers

The files in this folder are generated; don't edit them. In the [repository](https://github.com/zoran123456/lola-retro),
change the slang passes in `shaders/lola-retro/shaders`, then run `python3 tools/make_glsl.py`. How the
conversion works is described at the top of that script.

## License

MIT, see the LICENSE file in this folder.
