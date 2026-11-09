/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef XEEN_SCREEN_H
#define XEEN_SCREEN_H

#include "common/rect.h"
#include "graphics/screen.h"
#include "mm/xeen/font.h"
#include "mm/xeen/sprites.h"

namespace MM {
namespace Xeen {

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define PALETTE_COUNT 256
#define PALETTE_SIZE (256 * 3)

// 非公式日本語版: 日本語の字を東雲フォント 12×12 で描き直せるように、画面を 3 倍にして送る
#define JA_SCALE 3

class XeenEngine;

class Screen : public Graphics::Screen {
private:
	XeenEngine *_vm;
	byte _mainPalette[PALETTE_SIZE];
	byte _tempPalette[PALETTE_SIZE];
	XSurface _pages[2];
	XSurface _savedScreens[10];
	bool _fadeIn;

	/**
	 * 非公式日本語版: 画面に描いた日本語の字
	 */
	struct JaChar {
		int16 _x, _y;	// 8×8 のマスの左上（画面の座標）
		uint16 _cell;	// 字の表の番号（JIS X 0208 の区 × 94 ＋ 点）
		byte _color;	// 字の色
	};
	bool _jaHiRes;			// 画面を JA_SCALE 倍にして送る
	bool _jaShinonome;		// 日本語の字を東雲フォントで描き直す
	Common::Array<JaChar> _jaChars;
	Graphics::ManagedSurface _jaSurface;	// JA_SCALE 倍にした画面

	/**
	 * 日本語: 画面の一部を JA_SCALE 倍にして写す
	 */
	void jaScaleRect(const Common::Rect &r);

	/**
	 * 日本語: 8×8 の字がまだ画面に残っていれば true
	 */
	bool jaCharVisible(const JaChar &jc) const;

	/**
	 * 日本語: 広げた画面に、字を東雲フォントで描き直す
	 */
	void jaDrawChar(const JaChar &jc);

	/**
	 * Mark the entire screen for drawing
	 */
	void drawScreen();

	void fadeInner(int step);

	void updatePalette();

	void updatePalette(const byte *pal, int start, int count16);
public:
	Screen(XeenEngine *vm);
	~Screen() override {
	}

	/**
	 * Base method that descendent classes can override for recording affected
	 * dirty areas of the surface
	 */
	void addDirtyRect(const Common::Rect &r) override {
		Graphics::Screen::addDirtyRect(r);
	}

	/**
	 * 変わった所を実際の画面に送る
	 */
	void update() override;

	/**
	 * 日本語: JA_SCALE 倍で描く準備をする。initGraphics の前に呼ぶ
	 */
	void setupJa();

	/**
	 * 日本語: 画面を JA_SCALE 倍にして送っていれば true
	 */
	bool isJaHiRes() const { return _jaHiRes; }

	/**
	 * 日本語: 8×8 で描いた字の位置を覚える
	 */
	void addJaChar(int x, int y, int cell, byte color);

	/**
	 * 日本語: 東雲フォントと美咲フォントを切り替える
	 */
	void toggleJaFont();

	/**
	 * Load a palette resource into the temporary palette
	 */
	void loadPalette(const Common::Path &name);

	/**
	 * Load a background resource into memory
	 */
	void loadBackground(const Common::Path &name);

	/**
	 * Copy a loaded background into a display page
	 */
	void loadPage(int pageNum);

	void freePages();

	/**
	 * Merge the two pages along a horizontal split point
	 */
	void horizMerge(int xp = 0);

	/**
	 * Merge the two pages along a vertical split point
	 */
	void vertMerge(int yp);

	/**
	 * Fades in the screen
	 */
	void fadeIn(int step = 4);

	/**
	 * Fades out the screen
	 */
	void fadeOut(int step = 4);

	/**
	 * Saves a copy of the current screen into a specified slot
	 */
	void saveBackground(int slot = 1);

	/**
	 * Restores a previously saved screen
	 */
	void restoreBackground(int slot = 1);

	/**
	 * Draws the scroll in the background
	 * @param rollUp		If true, rolls up the scroll. If false, unrolls.
	 * @param fadeInFlag	If true, does an initial fade in
	 * @returns		True if key or mouse pressed
	 */
	bool doScroll(bool rollUp, bool fadeInFlag);
};

} // End of namespace Xeen
} // End of namespace MM

#endif
