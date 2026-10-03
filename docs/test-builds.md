# Experimental Windows builds

Solstice's **Windows x64 trial build** GitHub Actions workflow produces an
unsigned ZIP for testing. It is started manually by a repository maintainer;
ordinary pushes and pull requests do not start it.

Open [Actions](https://github.com/udtren/Solstice/actions/workflows/windows-build.yml),
select **Run workflow**, and choose the development branch. When a run succeeds,
download its **Solstice-windows-x64** artifact. Extract the artifact, then extract
the application ZIP inside it. Launch `bin/krita.exe` from the extracted folder;
the internal executable name remains unchanged for compatibility.

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
