#include "stats.hpp"
#include "font.hpp"
#include "game.hpp"
#include "gfx.hpp"
#include "kbd.hpp"
#include "settings.hpp"
#include "tonccpy.h"

#include <array>
#include <algorithm>
#include <nds.h>
#include <qrencode.h>

Stats::Stats(const std::string &path) : _path(path) {
	FILE *file = fopen(_path.c_str(), "r");
	if(!file)
		return;

	nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
	fclose(file);

	// Stats
	if(json.contains("guessCounts") && json["guessCounts"].is_array()) {
		for(const auto &item : json["guessCounts"]) {
			if(item.is_number())
				_guessCounts.push_back(item);
		}
	}
	if(json.contains("boardState") && json["boardState"].is_array()) {
		for(const auto &item : json["boardState"]) {
			if(item.is_string())
				_boardState.push_back(item);
		}
	}
	if(json.contains("streak") && json["streak"].is_number())
		_streak = json["streak"];
	if(json.contains("maxStreak") && json["maxStreak"].is_number())
		_maxStreak = json["maxStreak"];
	if(json.contains("gamesPlayed") && json["gamesPlayed"].is_number())
		_gamesPlayed = json["gamesPlayed"];
	if(json.contains("lastPlayed") && json["lastPlayed"].is_number())
		_lastPlayed = json["lastPlayed"];
	if(json.contains("lastWon") && json["lastWon"].is_number())
		_lastWon = json["lastWon"];
	else
		_lastWon = _lastPlayed;

	time_t today = time(NULL) / 24 / 60 / 60;
	if(_lastWon != today - 1 && _lastWon != today)
		_streak = 0;
	if(_streak > _maxStreak)
		_maxStreak = _streak;
	if(_lastPlayed != today)
		_boardState = {};
}

bool Stats::save() {
	nlohmann::json json({
		{"guessCounts", _guessCounts},
		{"boardState", _boardState},
		{"streak", _streak},
		{"maxStreak", _maxStreak},
		{"gamesPlayed", _gamesPlayed},
		{"lastPlayed", _lastPlayed},
		{"lastWon", _lastWon}
	});

	FILE *file = fopen(_path.c_str(), "w");
	if(file) {
		std::string dump = json.dump();
		size_t bytesWritten = fwrite(dump.c_str(), 1, dump.size(), file);
		fclose(file);

		return bytesWritten == dump.size();
	}

	return false;
}

void Stats::showQr() {
	// Ensure the game is done
	std::vector<TilePalette> allCorrect;
	for(size_t i = 0; i < game->answer().size(); i++)
		allCorrect.push_back(TilePalette::green);
	if((int)_boardState.size() < game->data().maxGuesses() && game->check(Font::utf8to16(_boardState.back())) != allCorrect)
		return;

	std::string str = shareMessage();

	QRcode *qr = QRcode_encodeString(str.c_str(), 0, QR_ECLEVEL_L, QR_MODE_8, true);

	// Draw QR
	int scale = SCREEN_HEIGHT / qr->width;
	u8 *dst = (u8 *)bgGetGfxPtr(BG_SUB(2)) + (SCREEN_HEIGHT - qr->width * scale) / 2 * SCREEN_WIDTH + (SCREEN_WIDTH - qr->width * scale) / 2;
	for(int y = 0; y < qr->width; y++) {
		for(int i = 0; i < scale; i++) // Fill line with white
			toncset(dst + (y * scale + i) * SCREEN_WIDTH - 4, TEXT_BLACK, qr->width * scale + 8);

		for(int x = 0; x < qr->width; x++) {
			if(qr->data[y * qr->width + x] & 1) { // If black, draw pixel
				for(int i = 0; i < scale; i++)
					toncset(dst + (y * scale + i) * SCREEN_WIDTH + (x * scale), TEXT_BLACK + 3, scale);
			}
		}
	}

	// Pad above and below with white
	for(int i = 0; i < 4; i++) {
		toncset(dst - (i + 1) * SCREEN_WIDTH - 4, TEXT_BLACK, qr->width * scale + 8);
		toncset(dst + ((qr->width * scale + i) * SCREEN_WIDTH) - 4, TEXT_BLACK, qr->width * scale + 8);
	}

	QRcode_free(qr);

	// Wait for input
	do {
		swiWaitForVBlank();
		scanKeys();
	} while(!(keysDown() & (KEY_A | KEY_B | KEY_TOUCH)));
}

std::string Stats::shareMessage() {
	char str[256];

	sprintf(str, "%s %lld %c/%d%s\n\n",
		game->data().shareName().c_str(),
		_lastPlayed - game->data().firstDay(),
		_guessCounts.back() > game->data().maxGuesses() ? 'X' : '0' + _guessCounts.back(),
		game->data().maxGuesses(),
		settings->hardMode() ? "*" : "");

	for(const std::string &_guess : _boardState) {
		std::vector<TilePalette> colors = game->check(Font::utf8to16(_guess));
		for(uint i = 0; i < colors.size(); i++)
			strcat(str, game->data().emoji(colors[i]).c_str());

		strcat(str, "\n");
	}

	return str;
}

void Stats::showMenu() {
	// Change to stats menu background
	game->data().statsBottom()
		.decompressTiles(bgGetGfxPtr(BG_SUB(0)))
		.decompressMap(bgGetMapPtr(BG_SUB(0)))
		.decompressPal(BG_PALETTE_SUB);

	while(1) {
		// Print scores
		game->data().numbersLarge().print(-96, 32, false, _gamesPlayed, Alignment::center);
		game->data().numbersLarge().print(-32, 32, false, std::count_if(_guessCounts.begin(), _guessCounts.end(), [](int a) { return a <= game->data().maxGuesses(); }) * 100 / _gamesPlayed, Alignment::center);
		game->data().numbersLarge().print(32, 32, false, _streak, Alignment::center);
		game->data().numbersLarge().print(96, 32, false, _maxStreak, Alignment::center).update(false);

		// Draw guess percentage bars
		int highestCount = 0;
		for(int i = 1; i <= game->data().maxGuesses(); i++)
			highestCount = std::max(highestCount, std::count(_guessCounts.begin(), _guessCounts.end(), i));

		for(int i = 1; i <= game->data().maxGuesses(); i++) {
			int count = std::count(_guessCounts.begin(), _guessCounts.end(), i);
			int width = (10 + (216 * count / highestCount));
			u8 palette = (i == _guessCounts.back() && game->won()) ? TEXT_WHITE_GREEN : TEXT_WHITE;
			u8 *dst = (u8 *)bgGetGfxPtr(BG_SUB(2)) + ((256 * (90 + (14 * i))) + 20);

			game->data().numbersSmall().print(8, 90 - 1 + i * 14, false, i);
			for(int j = 0; j < 12; j++) {
				int adjust = (j == 0 || j == 11) ? 1 : 0;
				toncset(dst + 256 * j + adjust, palette, width - adjust * 2);
			}

			game->data().numbersSmall().palette(palette).print(20 + width - 1, 90 - 1 + i * 14, false, count, Alignment::right).palette(TEXT_BLACK);
		}
		Font::update(false, true);

		u16 pressed;
		touchPosition touch;
		do {
			swiWaitForVBlank();
			scanKeys();
			pressed = keysDown();
			touchRead(&touch);
		} while(!(pressed & (KEY_A | KEY_B | KEY_TOUCH)));

		if(pressed & (KEY_A | KEY_B)) {
			break;
		} else if((pressed & KEY_TOUCH) && touch.py < 24) {
			if(touch.px < 24) {
				showQr();
				Font::clear(false);
				Font::update(false);
			} else if(touch.px > 232) {
				break;
			}
		}
	}

	Font::clear(false);
	Font::update(false);
}
