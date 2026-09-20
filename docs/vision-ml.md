# Vision ML

Vision ML provides native AI-assisted selection tools and filters, including
point-based and box-based segment selection, Smart Fill, and Background
Removal.

MobileSAM, MI-GAN, and BiRefNet GGUF models run through the embedded
`vision.cpp` runtime with a portable CPU backend and optional Vulkan
acceleration. BiRefNet Dynamic is preferred for background removal when
installed; the bundled BiRefNet Lite model is the fallback.

Implementation and model files: [`../plugins/visionml`](../plugins/visionml/)
