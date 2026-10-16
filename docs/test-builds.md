# Experimental Windows builds

Solstice's **Windows x64 trial build** GitHub Actions workflow produces an
unsigned ZIP for testing. It is started manually by a repository maintainer;
ordinary pushes and pull requests do not start it.

Open [Actions](https://github.com/udtren/Solstice/actions/workflows/windows-build.yml),
choose a successful run and download its `Solstice-windows-x64-<run number>` artifact
(downloading requires a GitHub account). Extract the artifact, then extract the
application ZIP inside it. Launch `Solstice.exe` in the extracted folder; it
starts `bin/solstice.exe`, which you can also start directly
(`bin/solstice.com` starts it from a console).

The ZIP includes a `shellex` folder with the Krita Shell Extension. Run
`shellex/register-thumbnails.cmd` to show `.kra` and `.krz` thumbnails in
Explorer for your account; see [Explorer thumbnails](windows-shell-thumbnails.md).

These are temporary development artifacts, not signed releases or an installer.
Artifacts expire after seven days. `BUILD-INFO.txt`, the dependency lock file,
and `SHA256SUMS.txt` accompany the ZIP. Windows may show a warning for an
unsigned application.

The workflow compiles the Vulkan engine and shaders, but does not verify GPU
rendering or Vulkan/OpenGL interoperation on supported hardware. Those checks
are done on a developer's machine. A successful CI build does not establish
runtime correctness or support for another GPU.

Build logs are uploaded separately, including on failures. No personal build
environment script, credentials, configuration, or artwork documents are
included in the workflow.

The CI dependency versions can differ from the local development build. Check
the included dependency lock and build logs when comparing behavior. Packaging
includes the application's QML modules as well as the Qt runtime.

The CI package includes Python plugin support and PyQt6, alongside the native
features, Vision ML, the Vulkan engine and the CPU G'MIC runtime. Python
support was restored in **0.2.0-beta.2**; its absence in the first beta is a
historical limitation. Configure Solstice includes the **Python Plugin Manager**.

For published downloads, use [GitHub Releases](https://github.com/udtren/Solstice/releases).
The temporary Actions artifacts described above are separate from release assets.
