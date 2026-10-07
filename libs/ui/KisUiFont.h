/*
 * SPDX-FileCopyrightText: 2021 Alvin Wong <alvin@alvinhc.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KIS_UI_FONT_H
#define KIS_UI_FONT_H

#include "kritaui_export.h"

#include <QFont>

namespace KisUiFont
{

/**
 * @brief Gets a font for normal UI widgets to use.
 * @return the UI font.
 */
KRITAUI_EXPORT QFont normalFont();

/**
 * @brief Gets a font with a smallish font size for dock widgets to use.
 * @return the small font.
 */
KRITAUI_EXPORT QFont dockFont();

/**
 * Solstice: registers the UI fonts embedded in the application (Cantarell,
 * krita/data/fonts) with the font database. Call before normalFont() is
 * first used; without them the UI keeps the system font.
 */
KRITAUI_EXPORT void registerBundledFonts();

} // namespace KisUiFont

#endif // KIS_UI_FONT_H
