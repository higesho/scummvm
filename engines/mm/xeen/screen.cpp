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

#include "common/config-manager.h"
#include "common/system.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"
#include "mm/xeen/screen.h"
#include "mm/xeen/resources.h"
#include "mm/xeen/window.h"
#include "mm/xeen/xeen.h"

namespace MM {
namespace Xeen {

Screen::Screen(XeenEngine *vm) : _vm(vm) {
	_fadeIn = false;
	_jaHiRes = false;
	_jaShinonome = false;
	create(SCREEN_WIDTH, SCREEN_HEIGHT);
	Common::fill(&_tempPalette[0], &_tempPalette[PALETTE_SIZE], 0);
	Common::fill(&_mainPalette[0], &_mainPalette[PALETTE_SIZE], 0);
}

/*------------------------------------------------------------------------*/
// 非公式日本語版: 東雲フォントでの描き直し
//
// 字はすべて窓（画面と同じ画素）に美咲フォント 8×8 で描かれる。そのとき addJaChar で位置を覚えておき、
// 画面を送るときに JA_SCALE 倍に広げて、まだ残っている字のマスを東雲フォント 12×12（1 ドットを 2×2）で描き直す。
// 字が消されたか（窓が閉じた、塗りつぶされた）は、送るたびに 8×8 の点が残っているかで確かめる。
// 窓を閉じて下の字が戻ってくることがあるので、確かめられなくても覚えた字は捨てない。
// 捨てるのは、同じ所に新しい字が描かれたときと、数が増えすぎたときだけ。

#define JA_MAX_CHARS 2000

void Screen::setupJa() {
	_jaHiRes = true;
	_jaSurface.create(SCREEN_WIDTH * JA_SCALE, SCREEN_HEIGHT * JA_SCALE);

	// 字の形: ini の xeen_ja_font（shinonome または misaki）。既定は東雲
	Common::String font = ConfMan.hasKey("xeen_ja_font") ? ConfMan.get("xeen_ja_font") : "shinonome";
	_jaShinonome = !font.equalsIgnoreCase("misaki");
}

void Screen::addJaChar(int x, int y, int cell, byte color) {
	if (!_jaHiRes)
		return;

	// 重なる字は消された（上に描かれた）ものとして捨てる
	Common::Rect r(x, y, x + FONT_WIDTH, y + FONT_HEIGHT);
	for (int i = (int)_jaChars.size() - 1; i >= 0; --i) {
		const JaChar &jc = _jaChars[i];
		if (r.intersects(Common::Rect(jc._x, jc._y, jc._x + FONT_WIDTH, jc._y + FONT_HEIGHT)))
			_jaChars.remove_at(i);
	}
	if (_jaChars.size() >= JA_MAX_CHARS)
		_jaChars.remove_at(0);

	JaChar jc;
	jc._x = x;
	jc._y = y;
	jc._cell = cell;
	jc._color = color;
	_jaChars.push_back(jc);
}

void Screen::toggleJaFont() {
	if (!_jaHiRes)
		return;

	_jaShinonome = !_jaShinonome;
	ConfMan.set("xeen_ja_font", _jaShinonome ? "shinonome" : "misaki", Common::ConfigManager::kApplicationDomain);
	ConfMan.flushToDisk();
	drawScreen();
	update();
}

void Screen::jaScaleRect(const Common::Rect &r) {
	for (int y = r.top; y < r.bottom; ++y) {
		const byte *srcP = (const byte *)getBasePtr(r.left, y);
		byte *destP = (byte *)_jaSurface.getBasePtr(r.left * JA_SCALE, y * JA_SCALE);
		byte *lineP = destP;
		for (int x = r.left; x < r.right; ++x, ++srcP) {
			for (int i = 0; i < JA_SCALE; ++i)
				*destP++ = *srcP;
		}

		// 同じ行を下に写す
		int len = r.width() * JA_SCALE;
		for (int i = 1; i < JA_SCALE; ++i)
			memcpy(lineP + i * _jaSurface.pitch, lineP, len);
	}
}

bool Screen::jaCharVisible(const JaChar &jc) const {
	if (!FontData::_jaFont || jc._x < 0 || jc._y < 0 || jc._x + FONT_WIDTH > SCREEN_WIDTH
			|| jc._y + FONT_HEIGHT > SCREEN_HEIGHT)
		return false;

	// 字の点はすべて字の色で、字のない所はほとんど字の色でない（塗りつぶされた所と区別する）
	const byte *glyph = FontData::_jaFont + jc._cell * FONT_HEIGHT;
	int dots = 0, sameColor = 0;
	for (int yp = 0; yp < FONT_HEIGHT; ++yp) {
		const byte *p = (const byte *)getBasePtr(jc._x, jc._y + yp);
		for (int xp = 0; xp < FONT_WIDTH; ++xp) {
			bool dot = (glyph[yp] & (0x80 >> xp)) != 0;
			if (dot) {
				if (p[xp] != jc._color)
					return false;
				++dots;
			} else if (p[xp] == jc._color) {
				++sameColor;
			}
		}
	}

	return dots > 0 && sameColor <= 8;
}

void Screen::jaDrawChar(const JaChar &jc) {
	const byte *glyph = FontData::_jaFont + jc._cell * FONT_HEIGHT;
	const byte *glyph12 = FontData::_jaFont12 + jc._cell * 24;

	// 字の背景の色（字の色でない点でいちばん多い色）
	uint counts[256];
	Common::fill(&counts[0], &counts[256], 0);
	for (int yp = 0; yp < FONT_HEIGHT; ++yp) {
		const byte *p = (const byte *)getBasePtr(jc._x, jc._y + yp);
		for (int xp = 0; xp < FONT_WIDTH; ++xp) {
			if (!(glyph[yp] & (0x80 >> xp)) && p[xp] != jc._color)
				++counts[p[xp]];
		}
	}
	int bg = 0;
	for (int i = 1; i < 256; ++i) {
		if (counts[i] > counts[bg])
			bg = i;
	}

	// 美咲の点を消す。となりの字でない点の色で埋める（背景に模様があっても目立たないように）
	static const int DX[4] = { -1, 1, 0, 0 }, DY[4] = { 0, 0, -1, 1 };
	for (int yp = 0; yp < FONT_HEIGHT; ++yp) {
		for (int xp = 0; xp < FONT_WIDTH; ++xp) {
			if (!(glyph[yp] & (0x80 >> xp)))
				continue;

			byte fill = bg;
			for (int d = 0; d < 4; ++d) {
				int nx = xp + DX[d], ny = yp + DY[d];
				if (nx < 0 || ny < 0 || nx >= FONT_WIDTH || ny >= FONT_HEIGHT)
					continue;
				if (glyph[ny] & (0x80 >> nx))
					continue;
				byte c = *(const byte *)getBasePtr(jc._x + nx, jc._y + ny);
				if (c != jc._color) {
					fill = c;
					break;
				}
			}

			for (int i = 0; i < JA_SCALE; ++i) {
				byte *destP = (byte *)_jaSurface.getBasePtr((jc._x + xp) * JA_SCALE,
					(jc._y + yp) * JA_SCALE + i);
				memset(destP, fill, JA_SCALE);
			}
		}
	}

	// 東雲の字を描く（1 ドットを 2×2。8 × 3 = 12 × 2 で、マスにちょうど入る）
	for (int yp = 0; yp < 12; ++yp) {
		uint bits = (glyph12[yp * 2] << 8) | glyph12[yp * 2 + 1];
		if (!bits)
			continue;
		for (int i = 0; i < 2; ++i) {
			byte *destP = (byte *)_jaSurface.getBasePtr(jc._x * JA_SCALE, jc._y * JA_SCALE + yp * 2 + i);
			for (int xp = 0; xp < 12; ++xp) {
				if (bits & (0x8000 >> xp)) {
					destP[xp * 2] = jc._color;
					destP[xp * 2 + 1] = jc._color;
				}
			}
		}
	}
}

void Screen::update() {
	if (!_jaHiRes) {
		Graphics::Screen::update();
		return;
	}

	bool redraw = _jaShinonome && FontData::_jaFont && FontData::_jaFont12;
	const Common::Rect screenBounds(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
	_dirtyRects.merge();

	for (Graphics::DirtyRectList::const_iterator i = _dirtyRects.begin(); i != _dirtyRects.end(); ++i) {
		Common::Rect r = *i;
		r.clip(screenBounds);
		if (r.isEmpty())
			continue;

		// 描き直す字のマスは、はみ出した分も含めて広げた画面を作り直す
		Common::Array<const JaChar *> chars;
		if (redraw) {
			// 広げた所にまた別の字がかかることがあるので、広がらなくなるまで繰り返す
			Common::Array<bool> taken;
			taken.resize(_jaChars.size());
			for (uint idx = 0; idx < taken.size(); ++idx)
				taken[idx] = false;

			for (bool grown = true; grown; ) {
				grown = false;
				for (uint idx = 0; idx < _jaChars.size(); ++idx) {
					const JaChar &jc = _jaChars[idx];
					Common::Rect cr(jc._x, jc._y, jc._x + FONT_WIDTH, jc._y + FONT_HEIGHT);
					if (taken[idx] || !cr.intersects(r) || !jaCharVisible(jc))
						continue;
					taken[idx] = true;
					chars.push_back(&jc);
					if (!r.contains(cr)) {
						r.extend(cr);
						grown = true;
					}
				}
			}
		}

		jaScaleRect(r);
		for (uint idx = 0; idx < chars.size(); ++idx)
			jaDrawChar(*chars[idx]);

		g_system->copyRectToScreen(_jaSurface.getBasePtr(r.left * JA_SCALE, r.top * JA_SCALE),
			_jaSurface.pitch, r.left * JA_SCALE, r.top * JA_SCALE,
			r.width() * JA_SCALE, r.height() * JA_SCALE);
	}

	updateScreen();
	_dirtyRects.clear();
}

/*------------------------------------------------------------------------*/

void Screen::loadPalette(const Common::Path &name) {
	File f(name);
	for (int i = 0; i < PALETTE_SIZE; ++i)
		_tempPalette[i] = f.readByte() << 2;
}

void Screen::loadBackground(const Common::Path &name) {
	File f(name);

	assert(f.size() == (SCREEN_WIDTH * SCREEN_HEIGHT));
	f.read((byte *)getPixels(), SCREEN_WIDTH * SCREEN_HEIGHT);
	addDirtyRect(Common::Rect(0, 0, this->w, this->h));
}

void Screen::loadPage(int pageNum) {
	assert(pageNum == 0 || pageNum == 1);
	if (_pages[0].empty()) {
		_pages[0].create(SCREEN_WIDTH, SCREEN_HEIGHT);
		_pages[1].create(SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	_pages[pageNum].blitFrom(*this);
}

void Screen::freePages() {
	_pages[0].free();
	_pages[1].free();
}

void Screen::horizMerge(int xp) {
	if (_pages[0].empty())
		return;

	for (int y = 0; y < SCREEN_HEIGHT; ++y) {
		byte *destP = (byte *)getBasePtr(0, y);
		const byte *srcP = (const byte *)_pages[0].getBasePtr(xp, y);
		Common::copy(srcP, srcP + SCREEN_WIDTH - xp, destP);

		if (xp != 0) {
			destP = (byte *)getBasePtr(SCREEN_WIDTH - xp, y);
			srcP = (const byte *)_pages[1].getBasePtr(0, y);
			Common::copy(srcP, srcP + xp, destP);
		}
	}

	markAllDirty();
}

void Screen::vertMerge(int yp) {
	if (_pages[0].empty())
		return;

	for (int y = 0; y < SCREEN_HEIGHT - yp; ++y) {
		const byte *srcP = (const byte *)_pages[0].getBasePtr(0, yp + y);
		byte *destP = (byte *)getBasePtr(0, y);
		Common::copy(srcP, srcP + SCREEN_WIDTH, destP);
	}

	for (int y = 0; y < yp; ++y) {
		const byte *srcP = (const byte *)_pages[1].getBasePtr(0, y);
		byte *destP = (byte *)getBasePtr(0, SCREEN_HEIGHT - yp + y);
		Common::copy(srcP, srcP + SCREEN_WIDTH, destP);
	}

	markAllDirty();
}

void Screen::drawScreen() {
	addDirtyRect(Common::Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT));
}

void Screen::fadeIn(int step) {
	_fadeIn = true;
	fadeInner(step);
}

void Screen::fadeOut(int step) {
	_fadeIn = false;
	fadeInner(step);
}

void Screen::fadeInner(int step) {
	for (int idx = 128; idx >= 0 && !_vm->shouldExit(); idx -= step) {
		int val = MAX(idx, 0);
		bool flag = !_fadeIn;
		if (!flag) {
			val = -(val - 128);
			flag = step != 0x81;
		}

		if (!flag) {
			step = 0x80;
		} else {
			// Create a scaled palette from the temporary one
			for (int i = 0; i < PALETTE_SIZE; ++i) {
				_mainPalette[i] = (_tempPalette[i] * val * 2) >> 8;
			}

			updatePalette();
		}

		_vm->_events->pollEventsAndWait();
	}

	update();
}

void Screen::updatePalette() {
	updatePalette(_mainPalette, 0, 16);
}

void Screen::updatePalette(const byte *pal, int start, int count16) {
	g_system->getPaletteManager()->setPalette(pal, start, count16 * 16);
}

void Screen::saveBackground(int slot) {
	assert(slot > 0 && slot < 10);
	_savedScreens[slot - 1].copyFrom(*this);
}

void Screen::restoreBackground(int slot) {
	assert(slot > 0 && slot < 10);

	blitFrom(_savedScreens[slot - 1]);
}

bool Screen::doScroll(bool rollUp, bool fadeInFlag) {
	Screen &screen = *_vm->_screen;
	EventsManager &events = *_vm->_events;
	const int SCROLL_L[8] = { 29, 23, 15, -5, -11, -23, -49, -71 };
	const int SCROLL_R[8] = { 165, 171, 198, 218, 228, 245, 264, 281 };

	if (_vm->_files->_ccNum) {
		if (fadeInFlag)
			screen.fadeIn(2);
		return _vm->shouldExit();
	}

	screen.saveBackground();

	// Load hand sprites
	SpriteResource *hand[16];
	for (int i = 0; i < 16; ++i) {
		Common::Path name(Common::String::format("hand%02d.vga", i));
		hand[i] = new SpriteResource(name);
	}

	// Load marb sprites
	SpriteResource *marb[5];
	for (int i = 0; i < 4; ++i) {
		Common::Path name(Common::String::format("marb%02d.vga", i + 1));
		marb[i] = new SpriteResource(name);
	}

	if (rollUp) {
		for (int i = 22, ctr = 7; i > 0 && !events.isKeyMousePressed()
			&& !_vm->shouldExit(); --i) {
			events.updateGameCounter();
			screen.restoreBackground();

			if (i > 14) {
				hand[14]->draw(0, 0, Common::Point(SCROLL_L[ctr], 0), SPRFLAG_800);
				hand[15]->draw(0, 0, Common::Point(SCROLL_R[ctr], 0), SPRFLAG_800);
				--ctr;
			} else if (i != 0) {
				hand[i - 1]->draw(0, 0);
			}

			if (i <= 20)
				marb[(i - 1) / 5]->draw(0, (i - 1) % 5);
			screen.update();

			while (!_vm->shouldExit() && events.timeElapsed() == 0)
				events.pollEventsAndWait();

			if (i == 0 && fadeInFlag)
				screen.fadeIn(2);
		}
	} else {
		for (int i = 0, ctr = 0; i < 22 && !events.isKeyMousePressed()
			&& !_vm->shouldExit(); ++i) {
			events.updateGameCounter();
			screen.restoreBackground();

			if (i < 14) {
				hand[i]->draw(0, 0);
			} else {
				hand[14]->draw(0, 0, Common::Point(SCROLL_L[ctr], 0), SPRFLAG_800);
				hand[15]->draw(0, 0, Common::Point(SCROLL_R[ctr], 0), SPRFLAG_800);
				++ctr;
			}

			if (i < 20) {
				marb[i / 5]->draw(0, i % 5);
			}
			screen.update();

			while (!_vm->shouldExit() && events.timeElapsed() == 0)
				events.pollEventsAndWait();

			if (i == 0 && fadeInFlag)
				screen.fadeIn(2);
		}
	}

	if (rollUp) {
		hand[0]->draw(0, 0);
		marb[0]->draw(0, 0);
	} else {
		screen.restoreBackground();
	}
	screen.update();

	// Free resources
	for (int i = 0; i < 4; ++i)
		delete marb[i];
	for (int i = 0; i < 16; ++i)
		delete hand[i];

	return _vm->shouldExit() || events.isKeyMousePressed();
}

} // End of namespace Xeen
} // End of namespace MM
