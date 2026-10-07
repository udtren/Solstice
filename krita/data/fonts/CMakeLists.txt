# Solstice: the bundled UI font is embedded through fonts.qrc (krita_QRCS).
# The font license is also installed as a readable file next to the data.
install(FILES OFL.txt
    RENAME Cantarell-OFL.txt
    DESTINATION ${KDE_INSTALL_DATADIR}/krita/fonts)
