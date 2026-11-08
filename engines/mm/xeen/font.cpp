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

#include "common/endian.h"
#include "mm/xeen/font.h"
#include "mm/xeen/resources.h"
#include "mm/xeen/xeen.h"

namespace MM {
namespace Xeen {

namespace {
const int kBig5Width = 16;
const int kBig5Height = 14;
}

const byte *FontData::_fontData;
Common::Point *FontData::_fontWritePos;
byte FontData::_textColors[4];
byte FontData::_bgColor;
bool FontData::_fontReduced;
Justify FontData::_fontJustify;
Graphics::Big5Font *FontData::_big5Font;
const byte *FontData::_jaFont;
	
FontSurface::FontSurface() : XSurface(), _msgWraps(false), _displayString(nullptr),
		_writePos(*FontData::_fontWritePos) {
	setTextColor(0);

	lang = g_vm->getLanguage();
	if (Common::RU_RUS == lang) {
		_fntEnOffset            = 0x0000;
		_fntNonEnOffset         = 0x0800;
		_fntEnReducedOffset     = 0x1000;
		_fntNonEnReducedOffset  = 0x1800;
		_fntEnWOffset           = 0x2000;
		_fntNonEnWOffset        = 0x2080;
		_fntEnReducedWOffset    = 0x2100;
		_fntNonEnReducedWOffset = 0x2180;
	} else {
		_fntEnOffset            = 0x0000;
		_fntEnReducedOffset     = 0x0800;
		_fntEnWOffset           = 0x1000;
		_fntEnReducedWOffset    = 0x1080;
	}
	_isBig5 = Common::ZH_TWN == lang;
	_isSjis = Common::JA_JPN == lang;
}

FontSurface::FontSurface(int wv, int hv) : XSurface(wv, hv),
		_msgWraps(false), _displayString(nullptr), _writePos(*FontData::_fontWritePos) {
	create(w, h);
	setTextColor(0);

	lang = g_vm->getLanguage();
	if (Common::RU_RUS == lang) {
		_fntEnOffset            = 0x0000;
		_fntNonEnOffset         = 0x0800;
		_fntEnReducedOffset     = 0x1000;
		_fntNonEnReducedOffset  = 0x1800;
		_fntEnWOffset           = 0x2000;
		_fntNonEnWOffset        = 0x2080;
		_fntEnReducedWOffset    = 0x2100;
		_fntNonEnReducedWOffset = 0x2180;
	} else {
		_fntEnOffset            = 0x0000;
		_fntEnReducedOffset     = 0x0800;
		_fntEnWOffset           = 0x1000;
		_fntEnReducedWOffset    = 0x1080;
	}
	_isBig5 = Common::ZH_TWN == lang;
	_isSjis = Common::JA_JPN == lang;
}

void FontSurface::writeSymbol(int symbolId) {
	const byte *srcP = &Res.SYMBOLS[symbolId][0];

	for (int yp = 0; yp < FONT_HEIGHT; ++yp) {
		byte *destP = (byte *)getBasePtr(_writePos.x, _writePos.y + yp);

		for (int xp = 0; xp < FONT_WIDTH; ++xp, ++destP) {
			byte b = *srcP++;
			if (b)
				*destP = b;
		}
	}

	_writePos.x += 8;
}

bool FontSurface::isSpace(char c) {
	if (Common::RU_RUS == lang || _isSjis)
		return c == ' ';
	return (c & 0x7f) == ' ';
}

const char *FontSurface::writeString(const Common::String &s, const Common::Rect &bounds, bool ttsVoiceText, Common::String *ttsMessage) {
	_displayString = s.c_str();
	assert(_fontData);

#ifdef USE_TTS
	bool deleteTTSMessage = false;
	if (!ttsMessage) {
		ttsMessage = new Common::String();
		deleteTTSMessage = true;
	}
#endif

	for (;;) {
		const char *msgStartP = _displayString;
		_msgWraps = false;

		// Get the size of the string that can be displayed on the line
		int xp = _fontJustify == JUSTIFY_CENTER ? bounds.left : _writePos.x;
		Common::Array<const char *> sjisStarts;
		const char *sjisOverP = nullptr;
		if (_isSjis) {
			// Japanese: remember where each character starts, so the line
			// can be broken between any two characters
			for (;;) {
				const char *charP = _displayString;
				if (getNextCharWidth(xp))
					break;
				if (xp >= bounds.right) {
					_displayString = charP;
					sjisOverP = charP;
					_msgWraps = true;
					break;
				}
				sjisStarts.push_back(charP);
			}
		} else {
			while (!getNextCharWidth(xp)) {
				if (xp >= bounds.right) {
					--_displayString;
					_msgWraps = true;
					break;
				}
			}
		}

		// Get the end point of the text that can be displayed
		const char *displayEnd = _displayString;
		_displayString = msgStartP;

		if (_isSjis && _msgWraps && _fontJustify != JUSTIFY_RIGHT) {
			const char *nextP = findSjisBreak(sjisStarts, sjisOverP);
			if (nextP == msgStartP) {
				// Not even one character fits
				if (_fontJustify == JUSTIFY_NONE && _writePos.x != bounds.left) {
					// Move to the next line
					if (!newLine(bounds))
						continue;
					// Ran out of space to display string
					break;
				}
				// Display the character anyway rather than looping forever
				_displayString = sjisOverP;
				getNextChar();
				nextP = _displayString;
				_displayString = msgStartP;
			}
			displayEnd = nextP - 1;
		} else if (_msgWraps && _fontJustify != JUSTIFY_RIGHT && xp >= bounds.right) {
			// Need to handle justification of text
			// First, move backwards to find the end of the previous word
			// for a convenient point to break the line at
			const char *endP = displayEnd;
			while (endP > _displayString && !isSpace(*endP))
				--endP;

			if (endP == _displayString) {
				// There was no word breaks at all in the string
				// Since the loop below includes displayEnd, we need to
				// subtract 1 to compensate and another time to delete last
				// char.
				displayEnd -= 2;
				if (_fontJustify == JUSTIFY_NONE && _writePos.x != bounds.left) {
					// Move to the next line
					if (!newLine(bounds))
						continue;
					// Ran out of space to display string
					break;
				}
			} else {
				// Found word break, find end of previous word
				while (endP > _displayString && !isSpace(*endP))
					--endP;

				displayEnd = endP;
			}
		}

		// Justification adjustment
		if (_fontJustify != JUSTIFY_NONE) {
			// Figure out the width of the selected portion of the string
			int totalWidth = 0;
			while (!getNextCharWidth(totalWidth)) {
				if (_displayString > displayEnd) {
					if (*displayEnd == ' ') {
						// Don't include any ending space as part of the total
						totalWidth -= _fontReduced ? 4 : 5;
					}
					break;
				}
			}

			// Reset starting position back to the start of the string portion
			_displayString = msgStartP;

			if (_fontJustify == JUSTIFY_RIGHT) {
				// Right aligned
				if (_writePos.x == bounds.left)
					_writePos.x = bounds.right;
				_writePos.x -= totalWidth + 1;
			} else {
				// Center aligned
				if (_writePos.x == bounds.left)
					_writePos.x = (bounds.left + bounds.right + 1 - totalWidth) / 2;
				else
					_writePos.x = (_writePos.x * 2 - totalWidth) / 2;
 			}
		}

		// Main character display loop
		while (_displayString <= displayEnd) {
			uint16_t c = getNextChar();

			if (c == ' ') {
				_writePos.x += _fontReduced ? 3 : 4;
#ifdef USE_TTS
				*ttsMessage += ' ';
#endif
			} else if (c == '\r') {
				fillRect(bounds, _bgColor);
				addDirtyRect(bounds);
				_writePos = Common::Point(bounds.left, bounds.top);
#ifdef USE_TTS
				*ttsMessage += '\n';
#endif
			} else if (c == 1) {
				// Turn off reduced font mode
				_fontReduced = false;
			} else if (c == 2) {
				// Turn on reduced font mode
				_fontReduced = true;
			} else if (c == 3) {
				// Justify text
				c = getNextChar();
				if (c == 'r')
					_fontJustify = JUSTIFY_RIGHT;
				else if (c == 'c')
					_fontJustify = JUSTIFY_CENTER;
				else
					_fontJustify = JUSTIFY_NONE;
			} else if (c == 4) {
				// Draw an empty box of a given width
				int wv = fontAtoi();
				Common::Point pt = _writePos;
				if (_fontJustify == JUSTIFY_RIGHT)
					pt.x -= wv;

				Common::Rect r(pt.x, pt.y, pt.x + wv, pt.y + (_fontReduced ? 9 : 10));
				fillRect(r, _bgColor);
			} else if (c == 5) {
				continue;
			} else if (c == 6) {
				// Non-breakable space
				writeChar(' ', bounds);
#ifdef USE_TTS
				*ttsMessage += ' ';
#endif
			} else if (c == 7) {
				// Set text background color
				int bgColor = fontAtoi();
				_bgColor = (bgColor < 0 || bgColor > 255) ? DEFAULT_BG_COLOR : bgColor;
			} else if (c == 8) {
				// Draw a character outline
				c = getNextChar();
				if (c == ' ') {
					c = '\0';
					_writePos.x -= 3;
				} else {
					if (c == 6)
						c = ' ';
					int offset_charW = c & 0x80 ?
						(_fontReduced ? _fntNonEnReducedWOffset : _fntNonEnWOffset) + (int)(c & 0x7F) :
						(_fontReduced ? _fntEnReducedWOffset : _fntEnWOffset) + (int)c;
					byte charSize = _fontData[offset_charW];

					_writePos.x -= charSize;
				}

				if (_writePos.x < bounds.left)
					_writePos.x = bounds.left;

				if (c) {
					int oldX = _writePos.x;
					byte oldColor[4];
					Common::copy(&_textColors[0], &_textColors[4], &oldColor[0]);

					_textColors[1] = _textColors[2] = _textColors[3] = _bgColor;
					writeChar(c, bounds);

					Common::copy(&oldColor[0], &oldColor[4], &_textColors[0]);
					_writePos.x = oldX;
				}
			} else if (c == 9) {
				// Skip x position
				int xAmount = fontAtoi();
				_writePos.x = MIN(bounds.left + xAmount, (int)bounds.right);
#ifdef USE_TTS
				*ttsMessage += '\n';
#endif
			} else if (c == 10) {
#ifdef USE_TTS
				*ttsMessage += '\n';
#endif

				// Newline
				if (newLine(bounds))
					return _displayString;
			} else if (c == 11) {
				// Set y position
				int yp = fontAtoi();
				_writePos.y = MIN(bounds.top + yp, (int)bounds.bottom);
#ifdef USE_TTS
				*ttsMessage += '\n';
#endif
			} else if (c == 12) {
				// Set text colors
				int idx = fontAtoi(2);
				if (idx < 0)
					idx = 0;
				setTextColor(idx);
			} else if (Common::RU_RUS == lang && (c & 0x80)) {
				writeChar(c, bounds);
#ifdef USE_TTS
				*ttsMessage += c;
#endif
			} else if (c < ' ') {
				// End of string or invalid command
				_displayString = nullptr;
#ifdef USE_TTS
				*ttsMessage += '\n';
#endif
				break;
			} else {
				// Standard character - write it out
				writeChar(c, bounds);
#ifdef USE_TTS
				*ttsMessage += c;
#endif
			}
		}

		if (!_displayString)
			break;
		if ( _displayString > displayEnd && _fontJustify != JUSTIFY_RIGHT && _msgWraps
				&& newLine(bounds))
			break;
	}

#ifdef USE_TTS
	if (ttsVoiceText) {
		g_vm->sayText(*ttsMessage);
	}

	if (deleteTTSMessage) {
		delete ttsMessage;
		ttsMessage = nullptr;
	}
#endif

	return _displayString;
}

const char *FontSurface::fitToWidth(const char *s, int maxWidth) {
	const char *strSave = _displayString;

	for (;;) {
		// Measure the rendered width of the string
		_displayString = s;
		int total = 0;
		while (*_displayString && !getNextCharWidth(total)) {
		}

		// writeString wraps once its running position reaches the right
		// edge, so the text only fits if it stays strictly narrower
		if (total < maxWidth || !*s)
			break;

		// Too wide, so drop the leading character and remeasure
		_displayString = s;
		getNextChar();
		s = _displayString;
	}

	_displayString = strSave;
	return s;
}

void FontSurface::writeCharacter(uint16_t c, const Common::Rect &clipRect) {
	Justify justify = _fontJustify;
	_fontJustify = JUSTIFY_NONE;
	if ((_isBig5 || _isSjis) && c >= 0x100)
		writeString(Common::String::format("%c%c", c >> 8, c & 0xff), clipRect);
	else
		writeString(Common::String::format("%c", c), clipRect);
	_fontJustify = justify;
}

// The French version stores accented characters as their code page 437
// codes, while its font keeps the corresponding glyphs in repurposed ASCII
// slots, so translate the former into the latter
static uint16_t frenchChar(byte c) {
	switch (c) {
	case 0x81: return 0x5E;   // u with diaeresis
	case 0x82: return 0x24;   // e with acute accent
	case 0x83: return 0x26;   // a with circumflex
	case 0x85: return 0x5D;   // a with grave accent
	case 0x87: return 0x7D;   // c with cedilla
	case 0x88: return 0x23;   // e with circumflex
	case 0x8A: return 0x25;   // e with grave accent
	case 0x8B: return 0x5F;   // i with diaeresis
	case 0x8C: return 0x7B;   // i with circumflex
	case 0x93: return 0x5B;   // o with circumflex
	case 0x96: return 0x3D;   // u with circumflex
	case 0x97: return 0x5C;   // u with grave accent
	case 0x80: return 'C';    // capital C with cedilla, which has no glyph
	case 0x90: return 'E';    // capital E with acute accent, which has no glyph
	default:
		// Never let an unknown high byte become a control code
		c &= 0x7f;
		return (c < ' ') ? ' ' : c;
	}
}

uint16_t FontSurface::getNextChar() {
	if (_isSjis) {
		byte lead = (byte)*_displayString++;
		if (((lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC)) && *_displayString)
			return (lead << 8) | ((byte)*_displayString++);
		return lead & 0x7f;
	} else if (_isBig5) {
		uint8_t lead = *_displayString++;
		if (!(lead & 0x80))
			return lead;
		return (lead << 8) | (*_displayString++ & 0xff);
	} else if (Common::RU_RUS == lang)
		return *_displayString++ & 0xff;
	else if (Common::FR_FRA == lang && (*_displayString & 0x80))
		return frenchChar(*_displayString++ & 0xff);
	else
		return *_displayString++ & 0x7f;
}

bool FontSurface::getNextCharWidth(int &total) {
	uint16_t c = getNextChar();

	if (_isBig5 && c > 0xff) {
		total += kBig5Width;
		return false;
	} else if (_isSjis && c > 0xff) {
		total += FONT_WIDTH;
		return false;
	} if (c > ' ') {
		total += _fontData[(_fontReduced ? _fntEnReducedWOffset : _fntEnWOffset) + (int)c];
		return false;
	} else if (c == ' ') {
		total += 4;
		return false;
	} else if (c == 8) {
		c = getNextChar();
		if (c == ' ') {
			total -= 2;
			return false;
		} else {
			_displayString -= 2;
			return true;
		}
	} else if (c == 12) {
		c = getNextChar();
		if (c != 'd')
			getNextChar();
		return false;
	} else if (Common::RU_RUS == lang && (c & 0x80)) {
		total += _fontData[(_fontReduced ? _fntNonEnReducedWOffset : _fntNonEnWOffset) + (int)(c & 0x7F)];
		return false;
	} else {
		--_displayString;
		return true;
	}
}

bool FontSurface::newLine(const Common::Rect &bounds) {
	// Move past any spaces currently being pointed to
	while (isSpace(*_displayString))
		++_displayString;

	_msgWraps = false;
	_writePos.x = bounds.left;

	int hv = _isBig5 && _big5Font ? _big5Font->getFontHeight() + 1 : (_fontReduced ? 9 : 10);
	_writePos.y += hv;

	return ((_writePos.y + hv - 1) > bounds.bottom);
}

int FontSurface::fontAtoi(int len) {
	int total = 0;
	for (int i = 0; i < len; ++i) {
		uint16_t c = getNextChar();
		if (c == ' ')
			c = '0';

		int digit = c - '0';
		if (digit < 0 || digit > 9)
			return -1;

		total = total * 10 + digit;
	}

	return total;
}

void FontSurface::setTextColor(int idx) {
	const byte *colP = (g_vm->_mode == MODE_STARTUP) ? &Res.TEXT_COLORS_STARTUP[idx][0] : &Res.TEXT_COLORS[idx][0];
	Common::copy(colP, colP + 4, &_textColors[0]);
}

/**
 * Converts a Shift-JIS code to the cell number of JIS X 0208, or -1
 */
static int sjisToCell(uint16_t c) {
	int hi = c >> 8, lo = c & 0xff;
	if (hi >= 0xE0)
		hi -= 0x40;
	int row = (hi - 0x81) * 2, col;
	if (lo >= 0x9F) {
		++row;
		col = lo - 0x9F;
	} else {
		col = lo - 0x40;
		if (lo >= 0x80)
			--col;
	}
	if (row < 0 || row >= 94 || col < 0 || col >= 94)
		return -1;
	return row * 94 + col;
}

static uint16_t sjisCharAt(const char *p) {
	byte lead = (byte)p[0];
	if (((lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC)) && p[1])
		return (lead << 8) | (byte)p[1];
	return lead & 0x7f;
}

// Characters that may not start a line
static bool sjisNoLineStart(uint16_t c) {
	static const char *const ASCII = ".,!?:;)]}'\"%";
	if (c < 0x80)
		return c && strchr(ASCII, c) != nullptr;
	static const uint16_t CHARS[] = {
		0x8141, 0x8142, 0x8143, 0x8144, 0x8145, 0x8146, 0x8147, 0x8148, 0x8149,
		0x814A, 0x814B, 0x8152, 0x8153, 0x8154, 0x8155, 0x8158, 0x815B,	
		0x815C, 0x815D, 0x8160, 0x8163, 0x8164,								
		0x8166, 0x8168, 0x816A, 0x816C, 0x816E, 0x8170, 0x8172, 0x8174, 0x8176,
		0x8178, 0x817A, 0x818B, 0x818C, 0x818D, 0x818E, 0x8193,				
		0x829F, 0x82A1, 0x82A3, 0x82A5, 0x82A7, 0x82C1, 0x82E1, 0x82E3, 0x82E5, 0x82EC,
		0x8340, 0x8342, 0x8344, 0x8346, 0x8348, 0x8362, 0x8383, 0x8385, 0x8387, 0x838E,
		0x8395, 0x8396, 0												
	};
	for (const uint16_t *p = CHARS; *p; ++p) {
		if (*p == c)
			return true;
	}
	return false;
}

// Characters that may not end a line
static bool sjisNoLineEnd(uint16_t c) {
	if (c < 0x80)
		return c == '(' || c == '[' || c == '{';
	static const uint16_t CHARS[] = {
		0x8165, 0x8167, 0x8169, 0x816B, 0x816D, 0x816F, 0x8171, 0x8173, 0x8175,
		0x8177, 0x8179, 0												
	};
	for (const uint16_t *p = CHARS; *p; ++p) {
		if (*p == c)
			return true;
	}
	return false;
}

static bool sjisIsWordChar(uint16_t c) {
	return c < 0x80 && (Common::isAlnum(c) || c == '-' || c == '\'');
}

const char *FontSurface::findSjisBreak(const Common::Array<const char *> &starts, const char *overP) {
	// all[i] is the start of the i-th character; the line is broken before
	// all[idx], so all[0] .. all[idx - 1] stay on this line
	Common::Array<const char *> all = starts;
	all.push_back(overP);
	int idx = (int)all.size() - 1;
	if (idx == 0)
		return all[0];

	// Keep English words (names, passwords) in one piece when possible
	if (sjisIsWordChar(sjisCharAt(all[idx])) && sjisIsWordChar(sjisCharAt(all[idx - 1]))) {
		int wordIdx = idx;
		while (wordIdx > 0 && sjisIsWordChar(sjisCharAt(all[wordIdx - 1])))
			--wordIdx;
		if (wordIdx > 0)
			return all[wordIdx];
	}

	// A space can start the next line, since newLine skips it
	if (sjisCharAt(all[idx]) == ' ')
		return all[idx];

	// Push the previous characters down rather than starting a line with
	// a closing bracket or punctuation, and don't end a line with an
	// opening bracket
	for (int tries = 0; tries < 3 && idx > 1; ++tries) {
		if (sjisNoLineStart(sjisCharAt(all[idx])) || sjisNoLineEnd(sjisCharAt(all[idx - 1])))
			--idx;
		else
			break;
	}

	return all[idx];
}

void FontSurface::writeSjisChar(uint16_t c, const Common::Rect &clipRect) {
	int cell = sjisToCell(c);
	if (_jaFont && cell >= 0) {
		const byte *srcP = _jaFont + cell * FONT_HEIGHT;
		for (int yp = 0; yp < FONT_HEIGHT; ++yp) {
			int y = _writePos.y + yp;
			if (y < clipRect.top || y >= clipRect.bottom)
				continue;

			byte bits = srcP[yp];
			byte *destP = (byte *)getBasePtr(_writePos.x, y);
			const byte *lineStart = (const byte *)getBasePtr(clipRect.left, y);
			const byte *lineEnd = (const byte *)getBasePtr(clipRect.right, y);

			for (int xp = 0; xp < FONT_WIDTH; ++xp, ++destP) {
				if ((bits & (0x80 >> xp)) && destP >= lineStart && destP < lineEnd)
					*destP = _textColors[2];
			}
		}
	}

	addDirtyRect(Common::Rect(_writePos.x, _writePos.y, _writePos.x + FONT_WIDTH,
		_writePos.y + FONT_HEIGHT));
	_writePos.x += FONT_WIDTH;
}

void FontSurface::writeChar(uint16_t c, const Common::Rect &clipRect) {
	// Get y position, handling kerning
	int y = _writePos.y;
	if (_isBig5 && c > 0xff) {
		_big5Font->drawBig5Char(surfacePtr(), c, _writePos, _textColors[0]);
		_writePos.x++;
		_big5Font->drawBig5Char(surfacePtr(), c, _writePos, _textColors[3]);
		_writePos.x += kBig5Width - 1;
		addDirtyRect(Common::Rect(_writePos.x, _writePos.y, _writePos.x + kBig5Width,
					  _writePos.y + kBig5Height));

		return;
	}
	if (_isSjis && c > 0xff) {
		writeSjisChar(c, clipRect);
		return;
	}
	if (c == 'g' || c == 'p' || c == 'q' || c == 'y')
		++y;
	if (_isBig5 && _big5Font) {
		y += (_big5Font->getFontHeight() - FONT_HEIGHT) / 2;
	}
	int yStart = y;

	// Get pointers into font data and surface to write pixels to
	int offset_charData;
	int offset_charW;
	if (Common::RU_RUS == lang && (c & 0x80)) {
		offset_charData = (_fontReduced ? _fntNonEnReducedOffset : _fntNonEnOffset) + (int)(c & 0x7F) * 16;
		offset_charW = (_fontReduced ? _fntNonEnReducedWOffset : _fntNonEnWOffset) + (int)(c & 0x7F);
	} else {
		offset_charData = (_fontReduced ? _fntEnReducedOffset : _fntEnOffset) + (int)c * 16;
		offset_charW = (_fontReduced ? _fntEnReducedWOffset : _fntEnWOffset) + (int)c;
	}
	const byte *srcP = &_fontData[offset_charData];

	for (int yp = 0; yp < FONT_HEIGHT; ++yp, ++y) {
		uint16 lineData = READ_LE_UINT16(srcP); srcP += 2;
		byte *destP = (byte *)getBasePtr(_writePos.x, y);

		// Ignore line if it's outside the clipping rect
		if (y < clipRect.top || y >= clipRect.bottom)
			continue;
		const byte *lineStart = (const byte *)getBasePtr(clipRect.left, y);
		const byte *lineEnd = (const byte *)getBasePtr(clipRect.right, y);

		for (int xp = 0; xp < FONT_WIDTH; ++xp, ++destP) {
			int colIndex = lineData & 3;
			lineData >>= 2;

			if (colIndex && destP >= lineStart && destP < lineEnd)
				*destP = _textColors[colIndex];
		}
	}

	addDirtyRect(Common::Rect(_writePos.x, yStart, _writePos.x + FONT_WIDTH,
		yStart + FONT_HEIGHT));
	_writePos.x += _fontData[offset_charW];
}

} // End of namespace Xeen
} // End of namespace MM
