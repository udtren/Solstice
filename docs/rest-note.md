# Rest Note

Rest Note is a native timer docker with working, paused, idle, eye-break, and
full-break states. It retains the original plugin's icons and configuration.

The small eye-break notification appears on Krita's current screen. The large
break overlay is limited to the Krita window rather than covering the entire
monitor.

Rest Note is off by default. Turn it on in **Configure Solstice > General >
Custom > Enable the Rest Note docker**, restart Solstice, and show it from
**Settings > Dockers**. Turning the option off stops the timers and any break
notice at once and hides the docker; it disappears from the Dockers menu
after the next restart.

Native implementation: [`../plugins/dockers/restnote`](../plugins/dockers/restnote/)
