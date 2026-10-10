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
- Strokes drawn with ABR tips on a [Brush Stroke Layer](brush-stroke-layer.md)
  save their tips in the `.kra` file.

## Not read yet

- Photoshop's brush presets (size, spacing, pressure dynamics, scattering,
  texture, dual brush, color dynamics): planned. Until then each tip has to
  be set up as a brush in the Brush Editor.
- Folders of presets: planned, as tags.
- Computed (round) brushes of ABR versions 1 and 2.

## Known limitations

- ABR libraries imported before this version keep the tip names they had;
  removing the library and importing the file again shows the new names.
