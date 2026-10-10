# Photoshop Brushes (ABR)

Solstice reads Photoshop brush files (`.abr`) added in **Settings > Manage
Resource Libraries > Import**. Each `.abr` file appears there as a library of
its own. Improved ABR support is being added in stages.

## What is read now

- The brush tip images of ABR versions 1, 2 and 6 to 10, including 16-bit
  tips and compressed tips. They appear among the predefined brush tips in
  the Brush Editor.
- Tips are named after the Photoshop preset that uses them. Tips that no
  preset uses, and tips of older files, are named after the file and a
  number (`brushes_12`).
- Damaged files are read as far as possible; unreadable tips are skipped
  and reported in the log.
- Patterns stored in ABR files appear with the other patterns, for fills and
  brush textures. Patterns in color modes Solstice cannot read (such as
  CMYK) are skipped and reported in the log.
- Photoshop's brush presets become Pixel Brush presets, listed under the
  `.abr` file's name like a bundle. They take the tip, size, angle,
  roundness, spacing and hardness; size, angle, roundness, opacity and flow
  dynamics (pressure, tilt, wheel, direction, rotation, fade and jitter);
  scattering; texture (with the file's pattern and Photoshop's blend modes);
  the dual brush (as the masking brush); and the tool's opacity, flow and
  blend mode. They paint in Wash mode.
- Strokes drawn with ABR tips on a [Brush Stroke Layer](brush-stroke-layer.md)
  save their tips in the `.kra` file.

## Not read yet

- Some Photoshop settings have no counterpart and are left out (the log
  lists them for each file): the scatter count, flipping and flip jitter,
  color dynamics, wet edges, noise, brush pose, texture protection and
  smoothing.
- Folders of presets: planned, as tags.
- Computed (round) brushes of ABR versions 1 and 2.

## Known limitations

- Presets with a shallow texture depth in Photoshop's Height mode, or with a
  dual brush in Darken or Color Burn mode, may paint very faintly.

- ABR libraries imported before this version keep the tip names they had;
  removing the library and importing the file again shows the new names.
