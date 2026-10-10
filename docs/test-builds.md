# Experimental Windows builds

Solstice's **Windows x64 trial build** GitHub Actions workflow produces an
unsigned ZIP for testing. It is started manually by a repository maintainer;
ordinary pushes and pull requests do not start it.

Open [Actions](https://github.com/udtren/Solstice/actions/workflows/windows-build.yml),
select **Run workflow**, and choose the development branch. When a run succeeds,
download its **Solstice-windows-x64** artifact. Extract the artifact, then extract
the application ZIP inside it. Launch `bin/krita.exe` from the extracted folder;
the internal executable name remains unchanged for compatibility.

The ZIP includes a `shellex` folder with the Krita Shell Extension. Run
`shellex/register-thumbnails.cmd` to show `.kra` and `.krz` thumbnails in
Explorer for your account; see [Explorer thumbnails](windows-shell-thumbnails.md).

These are temporary development artifacts, not signed releases or an installer.
Artifacts expire after seven days. `BUILD-INFO.txt`, the dependency lock file,
and `SHA256SUMS.txt` accompany the ZIP. Windows may show a warning for an
unsigned application.

The workflow compiles the Vulkan engine and shaders, but does not verify GPU
rendering or Vulkan/OpenGL interoperation on supported hardware. Those checks
still require the Windows desktop test environment. A successful CI build
does not establish runtime correctness or support for another GPU.

Build logs are uploaded separately, including on failures. No personal build
environment script, credentials, configuration, or artwork documents are
included in the workflow.

The CI dependency versions can differ from the local development build. Check
the included dependency lock and build logs when comparing behavior. Packaging
includes the application's QML modules as well as the Qt runtime.

The initial CI dependency configuration uses Qt 6.11.0 and does not enable
Python plugins: its configure step cannot find SIP and PyQt6. Native features
and the Vulkan engine are built, but this trial is not yet a feature-complete
replacement for the local development installation.

The [first successful trial](https://github.com/udtren/Solstice/actions/runs/37131426139)
completed on October 4, 2026 (JST), in about 48 minutes. Its downloaded ZIP
passed checksum and archive-integrity checks, including verification that GPU,
Vision ML, Qt and application QML files were present. Application execution
and GPU rendering were not tested as part of this CI verification.
