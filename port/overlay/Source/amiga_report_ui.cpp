// F10 bug report of the Warp port: see amiga_report_ui.hpp.
//
// The drawer <save dir>/bugNNN/ with bugNNN.txt (version, machine, hero),
// bugNNN-screen.bmp (the last frame), bugNNN.sv (the save) and bugNNN.ini
// (the settings); NNN = the first number with no drawer bugNNN and no old
// file bugNNN.txt in the save dir; the hero list does not show them (it reads
// single_N.sv of the save dir only).
//
// Then a window in a frame in the middle of the screen (no messages over the
// game, no "Pause" box - the game is paused under it): the e-mail
// (remembered in <config dir>/report.ini), the words, the sending to the game
// server (port/sdl/src/amiga_report.c, a process of its own) and its result.
// Keys: Enter - from the e-mail to the words / a new line, Enter on an
// empty line or F10 - send, Backspace, Esc - do not send (the drawer stays).
#include "amiga_report_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "amiga_version.h"
#include "diablo.h"
#include "effects.h"
#include "engine/backbuffer_state.hpp"
#include "engine/palette.h"
#include "engine/render/amiga_cells.hpp"
#include "engine/render/primitive_render.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "gamemenu.h"
#include "levels/dun_tile_data.hpp"
#include "player.h"
#include "utils/display.h"
#include "utils/file_util.h"
#include "utils/language.h"
#include "utils/paths.h"
#include "utils/sdl_compat.h"
#include "utils/str_cat.hpp"

extern "C" const char *amiga_save_path(void); // pfile.cpp
extern "C" int amiga_machine_info(char *buf, int size);
extern "C" int Report_Send(const char *host, int port, const char *version, const char *kind, const char *email,
    const char *text, const char *machine, const char *const *files, int nfiles);
extern "C" int Report_Result(char *text, int size, int *ok);
extern "C" int Report_Network(void);

namespace devilution {

namespace {

enum class Step : uint8_t {
	None,
	Email,   // typing the e-mail
	Words,   // typing what happened
	Sending, // waiting for the sending process
	Result,  // the result (or the drawer only); Enter / Esc closes
};

Step step = Step::None;
bool paused = false; // the report paused the game (and unpauses it at its end)
std::string reportName, reportFolder, reportPath, email;
std::vector<std::string> words; // the lines; the last one is being typed
std::string resultLine1, resultLine2;
bool resultOk = false;

constexpr size_t EmailMax = 80;
constexpr size_t WordsMax = 1000;
constexpr int BoxWidth = 560;
constexpr int BoxHeight = 262;
constexpr int WordsLines = 6;
constexpr int LineWidth = BoxWidth - 56; // the text area, in pixels
constexpr const char *ServerDefault = "83.12.152.135:6810";

bool Polish()
{
	return GetLanguageCode().substr(0, 2) == "pl";
}

const char *Tr(const char *en, const char *pl)
{
	return Polish() ? pl : en;
}

std::string Setting(const char *key, const char *def)
{
	std::string value = def;
	if (FILE *f = std::fopen((paths::ConfigPath() + "report.ini").c_str(), "r")) {
		char line[256];
		const size_t klen = std::strlen(key);
		while (std::fgets(line, sizeof(line), f) != nullptr) {
			if (std::strncmp(line, key, klen) == 0 && line[klen] == '=') {
				value = line + klen + 1;
				while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
					value.pop_back();
			}
		}
		std::fclose(f);
	}
	return value;
}

/** One typed character (UTF-8 of a Latin-1 code from msdl_events.c) as ASCII for the server:
    Polish letters of an ISO-8859-2 keymap and of real UTF-8, German ones, without their marks. */
std::string Ascii(std::string_view in)
{
	static const struct {
		unsigned code;
		const char *ascii;
	} map[] = {
		// ISO-8859-2 (Polish keymap) read as Latin-1
		{ 0xB1, "a" }, { 0xE6, "c" }, { 0xEA, "e" }, { 0xB3, "l" }, { 0xF1, "n" }, { 0xF3, "o" }, { 0xB6, "s" },
		{ 0xBC, "z" }, { 0xBF, "z" }, { 0xA1, "A" }, { 0xC6, "C" }, { 0xCA, "E" }, { 0xA3, "L" }, { 0xD1, "N" },
		{ 0xD3, "O" }, { 0xA6, "S" }, { 0xAC, "Z" }, { 0xAF, "Z" },
		// German (the same codes in both)
		{ 0xE4, "ae" }, { 0xF6, "oe" }, { 0xFC, "ue" }, { 0xDF, "ss" }, { 0xC4, "Ae" }, { 0xD6, "Oe" }, { 0xDC, "Ue" },
		// Unicode Polish letters
		{ 0x105, "a" }, { 0x107, "c" }, { 0x119, "e" }, { 0x142, "l" }, { 0x144, "n" }, { 0x15B, "s" }, { 0x17A, "z" },
		{ 0x17C, "z" }, { 0x104, "A" }, { 0x106, "C" }, { 0x118, "E" }, { 0x141, "L" }, { 0x143, "N" }, { 0x15A, "S" },
		{ 0x179, "Z" }, { 0x17B, "Z" },
	};
	std::string out;
	for (size_t i = 0; i < in.size();) {
		const unsigned char c = static_cast<unsigned char>(in[i]);
		unsigned code;
		size_t n;
		if (c < 0x80) {
			code = c;
			n = 1;
		} else if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) {
			code = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3Fu);
			n = 2;
		} else {
			code = 0xFFFF;
			n = 1;
			while (i + n < in.size() && (static_cast<unsigned char>(in[i + n]) & 0xC0) == 0x80)
				n++;
		}
		i += n;
		if (code >= 32 && code < 127) {
			out += static_cast<char>(code);
			continue;
		}
		const char *a = "?";
		for (const auto &m : map)
			if (m.code == code) a = m.ascii;
		out += a;
	}
	return out;
}

size_t WordsSize()
{
	size_t n = 0;
	for (const std::string &l : words)
		n += l.size() + 1;
	return n;
}

void Pause()
{
	if (PauseMode == 0) {
		PauseMode = 2;
		sound_stop();
		LastPlayerAction = PlayerActionType::None;
		paused = true;
	}
	RedrawEverything();
}

void Close()
{
	step = Step::None;
	if (paused && PauseMode == 2)
		PauseMode = 0;
	paused = false;
	RedrawEverything();
}

void ShowResult(bool ok, std::string line1, std::string line2)
{
	resultOk = ok;
	resultLine1 = std::move(line1);
	resultLine2 = std::move(line2);
	step = Step::Result;
}

void Start(const std::string &name, const std::string &folder, const std::string &path, bool saveOk)
{
	reportName = name;
	reportFolder = folder;
	reportPath = path;
	words.assign(1, std::string());
	email = Setting("email", "");
	Pause();
	if (!saveOk)
		SDL_Log("Bug report %s: no copy of the save", name.c_str());
	if (!Report_Network()) {
		ShowResult(false, Tr("No network (TCP/IP): the report stays on the disk.", "Brak sieci (TCP/IP): zgłoszenie zostaje na dysku."),
		    Tr("Add what happened to the .txt file and send the drawer by e-mail.", "Dopisz w pliku .txt, co się stało, i wyślij szufladę e-mailem."));
		return;
	}
	step = Step::Email;
}

void Send()
{
	std::string text;
	for (const std::string &l : words)
		if (!l.empty()) text += l + "\n";
	if (text.size() > WordsMax) text.resize(WordsMax);
	// the words also into bugNNN.txt
	if (FILE *t = std::fopen((reportPath + ".txt").c_str(), "a")) {
		std::fputs(text.c_str(), t);
		std::fclose(t);
	}
	std::string server = Setting("server", ServerDefault);
	int port = 6810;
	if (const size_t colon = server.rfind(':'); colon != std::string::npos) {
		port = std::atoi(server.c_str() + colon + 1);
		server.resize(colon);
	}
	char machine[256];
	amiga_machine_info(machine, sizeof(machine));
	machine[119] = '\0'; // the server keeps 120
	const std::string files[4] = { reportPath + ".txt", reportPath + "-screen.bmp", reportPath + ".sv", reportPath + ".ini" };
	const char *names[4] = { files[0].c_str(), files[1].c_str(), files[2].c_str(), files[3].c_str() };
	if (Report_Send(server.c_str(), port, "Diablo-" DIABLO_WARP_VERSION, "F10", email.c_str(), text.c_str(), machine, names, 4)) {
		step = Step::Sending;
	} else {
		SDL_Log("Bug report %s: not sent (no sending process)", reportName.c_str());
		ShowResult(false, Tr("The report could not be sent.", "Nie udało się wysłać zgłoszenia."),
		    Tr("It stays on the disk.", "Zostaje na dysku."));
	}
}

void EmailDone()
{
	std::string clean;
	for (const char c : email)
		if (std::isalnum(static_cast<unsigned char>(c)) || std::strchr("@._-+", c) != nullptr)
			clean += c;
	email = clean.substr(0, EmailMax);
	const std::string server = Setting("server", ServerDefault);
	if (FILE *f = std::fopen((paths::ConfigPath() + "report.ini").c_str(), "w")) {
		std::fprintf(f, "email=%s\nserver=%s\n", email.c_str(), server.c_str());
		std::fclose(f);
	}
	step = Step::Words;
}

/** A character typed into the words: a new line when the line gets too wide (at its last space). */
void TypeWords(const std::string &s)
{
	for (const char c : s) {
		if (WordsSize() >= WordsMax)
			return;
		std::string &line = words.back();
		line += c;
		if (GetLineWidth(line, GameFont12, 1) > LineWidth) {
			const size_t space = line.rfind(' ');
			std::string rest;
			if (space != std::string::npos && space > 0) {
				rest = line.substr(space + 1);
				line.resize(space);
			} else {
				rest = line.substr(line.size() - 1);
				line.pop_back();
			}
			words.push_back(rest);
		}
	}
}

void Key(SDL_Keycode key)
{
	switch (step) {
	case Step::Email:
		if (key == SDLK_ESCAPE) {
			SDL_Log("Bug report %s: not sent (Esc)", reportName.c_str());
			Close();
		} else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_TAB) {
			EmailDone();
		} else if (key == SDLK_F10) {
			EmailDone();
			Send();
		} else if (key == SDLK_BACKSPACE && !email.empty()) {
			email.pop_back();
		}
		break;
	case Step::Words:
		if (key == SDLK_ESCAPE) {
			SDL_Log("Bug report %s: not sent (Esc)", reportName.c_str());
			Close();
		} else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
			if (words.back().empty())
				Send();
			else if (WordsSize() < WordsMax)
				words.emplace_back();
		} else if (key == SDLK_F10) {
			Send();
		} else if (key == SDLK_BACKSPACE) {
			if (!words.back().empty())
				words.back().pop_back();
			else if (words.size() > 1)
				words.pop_back();
		} else if (key == SDLK_UP) {
			step = Step::Email;
		}
		break;
	case Step::Result:
		if (key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
			Close();
		break;
	default:
		break;
	}
}

} // namespace

bool AmigaReportActive()
{
	return step != Step::None;
}

bool AmigaReportHandleEvent(const SDL_Event &event)
{
	if (step == Step::None)
		return false;
	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		Key(SDLC_EventKey(event));
		return true;
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return true;
	case SDL_EVENT_TEXT_INPUT: {
		const std::string s = Ascii(event.text.text);
		if (step == Step::Email) {
			for (const char c : s)
				if (email.size() < EmailMax && c != ' ') email += c;
		} else if (step == Step::Words) {
			TypeWords(s);
		}
		return true;
	}
	default:
		return false;
	}
}

void AmigaReportPoll()
{
	if (step != Step::Sending)
		return;
	char text[80];
	int ok = 0;
	if (!Report_Result(text, sizeof(text), &ok))
		return;
	SDL_Log("Bug report %s: %s %s", reportName.c_str(), ok ? "sent, number" : "not sent:", text);
	if (ok)
		ShowResult(true, StrCat(Tr("Report no. ", "Zgłoszenie nr "), text, Tr(" sent. Thank you!", " wysłane. Dziękujemy!")), "");
	else
		ShowResult(false, StrCat(Tr("Report not sent: ", "Zgłoszenie nie zostało wysłane: "), text),
		    Tr("It stays on the disk.", "Zostaje na dysku."));
}

void AmigaReportDraw(const Surface &out)
{
	if (step == Step::None)
		return;
	const int x = (out.w() - BoxWidth) / 2;
	const int viewHeight = out.h() - 128; // above the panel
	const int y = std::max(4, (viewHeight - BoxHeight) / 2);
	amiga_cells::RecordOverlay(out, x, y, BoxWidth, BoxHeight);
	FillRect(out, x, y, BoxWidth, BoxHeight, 0);
	UnsafeDrawBorder2px(out, Rectangle { { x, y }, { BoxWidth, BoxHeight } }, PAL16_YELLOW + 4);

	const int lx = x + 20, lw = BoxWidth - 40;
	int ly = y + 12;
	DrawString(out, StrCat(Tr("Bug report ", "Zgłoszenie błędu "), reportName), { { x, ly }, { BoxWidth, 0 } },
	    { .flags = UiFlags::ColorGold | UiFlags::AlignCenter });
	ly += 20;
	DrawString(out, StrCat(Tr("Saved in ", "Zapisane w "), reportFolder), { { lx, ly }, { lw, 0 } },
	    { .flags = UiFlags::ColorWhite });
	ly += 22;

	if (step == Step::Result) {
		ly += 30;
		DrawString(out, resultLine1, { { lx, ly }, { lw, 0 } },
		    { .flags = (resultOk ? UiFlags::ColorGold : UiFlags::ColorRed) | UiFlags::AlignCenter });
		ly += 20;
		if (!resultLine2.empty())
			DrawString(out, resultLine2, { { lx, ly }, { lw, 0 } }, { .flags = UiFlags::ColorWhite | UiFlags::AlignCenter });
		DrawString(out, Tr("Enter or Esc: back to the game", "Enter albo Esc: powrót do gry"), { { lx, y + BoxHeight - 24 }, { lw, 0 } },
		    { .flags = UiFlags::ColorGold | UiFlags::AlignCenter });
		return;
	}

	const bool blink = (SDL_GetTicks() / 400) % 2 == 0;
	// the e-mail
	DrawString(out, Tr("Your e-mail (may be empty):", "Twój e-mail (może być pusty):"), { { lx, ly }, { lw, 0 } },
	    { .flags = UiFlags::ColorGold });
	ly += 16;
	const uint8_t frameOn = PAL16_YELLOW + 4, frameOff = PAL16_GRAY + 8;
	UnsafeDrawBorder2px(out, Rectangle { { lx, ly }, { lw, 20 } }, step == Step::Email ? frameOn : frameOff);
	DrawString(out, email + (step == Step::Email && blink ? "_" : ""), { { lx + 8, ly + 4 }, { lw - 16, 0 } },
	    { .flags = UiFlags::ColorWhite });
	ly += 28;
	// the words
	DrawString(out, Tr("What happened:", "Co się stało:"), { { lx, ly }, { lw, 0 } }, { .flags = UiFlags::ColorGold });
	ly += 16;
	const int areaHeight = WordsLines * 14 + 8;
	UnsafeDrawBorder2px(out, Rectangle { { lx, ly }, { lw, areaHeight } }, step == Step::Words ? frameOn : frameOff);
	const size_t first = words.size() > WordsLines ? words.size() - WordsLines : 0;
	for (size_t i = first; i < words.size(); i++) {
		const bool last = i + 1 == words.size();
		DrawString(out, words[i] + (last && step == Step::Words && blink ? "_" : ""),
		    { { lx + 8, ly + 4 + static_cast<int>(i - first) * 14 }, { lw - 16, 0 } }, { .flags = UiFlags::ColorWhite });
	}
	ly += areaHeight + 8;
	// help or the sending (two lines at most)
	const char *help1 = "";
	const char *help2;
	if (step == Step::Sending) {
		help2 = Tr("Sending the report to the game server...", "Wysyłam zgłoszenie na serwer gier...");
	} else if (step == Step::Email) {
		help2 = Tr("Enter: next.  Esc: do not send (the files stay).", "Enter: dalej.  Esc: nie wysyłaj (pliki zostają).");
	} else {
		help1 = Tr("Enter: new line.  Enter on an empty line or F10: send.", "Enter: nowa linia.  Enter w pustej linii albo F10: wyślij.");
		help2 = Tr("Esc: do not send (the files stay).", "Esc: nie wysyłaj (pliki zostają).");
	}
	DrawString(out, help1, { { lx, y + BoxHeight - 40 }, { lw, 0 } }, { .flags = UiFlags::ColorGold | UiFlags::AlignCenter });
	DrawString(out, help2, { { lx, y + BoxHeight - 24 }, { lw, 0 } }, { .flags = UiFlags::ColorGold | UiFlags::AlignCenter });
}

void AmigaBugReport()
{
	if (gbIsMultiplayer || !gbRunGame || MyPlayer == nullptr || step != Step::None)
		return;
	const std::string base = paths::PrefPath();
	char name[8];
	int num = -1;
	for (int i = 1; i < 1000; i++) {
		std::snprintf(name, sizeof(name), "bug%03d", i);
		if (!DirectoryExists(base + name) && !FileExists(base + name + ".txt")) {
			num = i;
			break;
		}
	}
	if (num < 0 || !CreateDir((base + name).c_str())) {
		reportName = "";
		reportFolder = base;
		Pause();
		ShowResult(false, Tr("No drawer for the report in the save directory.", "Brak miejsca na szufladę zgłoszenia w katalogu zapisów."), "");
		return;
	}
	const std::string dir = base + name + "/" + name;
	const auto copy = [](const std::string &from, const std::string &to) {
		FILE *in = std::fopen(from.c_str(), "rb");
		if (in == nullptr) return false;
		FILE *out = std::fopen(to.c_str(), "wb");
		if (out == nullptr) {
			std::fclose(in);
			return false;
		}
		char buf[8192];
		size_t n;
		bool ok = true;
		while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)
			if (std::fwrite(buf, 1, n, out) != n) ok = false;
		std::fclose(in);
		if (std::fclose(out) != 0) ok = false;
		return ok;
	};
	// the last frame (8-bit, the game's palette) as an uncompressed BMP,
	// before the save draws its "Saving..." box over it
	bool shot = false;
	if (PalSurface != nullptr && PalSurface->format->BitsPerPixel == 8) {
		if (FILE *f = std::fopen((dir + "-screen.bmp").c_str(), "wb")) {
			const int w = PalSurface->w, h = PalSurface->h, row = (w + 3) & ~3;
			const uint32_t data = 54 + 1024, size = data + static_cast<uint32_t>(row) * h;
			uint8_t hdr[54] = { 'B', 'M' };
			const auto le = [&hdr](int at, uint32_t v, int bytes) {
				for (int k = 0; k < bytes; k++) hdr[at + k] = static_cast<uint8_t>(v >> (8 * k));
			};
			le(2, size, 4);
			le(10, data, 4);
			le(14, 40, 4);
			le(18, w, 4);
			le(22, h, 4);
			le(26, 1, 2);
			le(28, 8, 2);
			le(34, static_cast<uint32_t>(row) * h, 4);
			le(46, 256, 4);
			std::fwrite(hdr, 1, sizeof(hdr), f);
			for (const SDL_Color &c : system_palette) {
				const uint8_t q[4] = { c.b, c.g, c.r, 0 };
				std::fwrite(q, 1, 4, f);
			}
			std::vector<uint8_t> line(row, 0);
			for (int y = h - 1; y >= 0; y--) {
				std::memcpy(line.data(), static_cast<const uint8_t *>(PalSurface->pixels) + y * PalSurface->pitch, w);
				std::fwrite(line.data(), 1, row, f);
			}
			shot = std::fclose(f) == 0;
		}
	}
	gamemenu_save_game(false);
	const bool ok = copy(amiga_save_path(), dir + ".sv");
	copy(paths::ConfigPath() + "diablo.ini", dir + ".ini");
	if (FILE *t = std::fopen((dir + ".txt").c_str(), "w")) {
		char machine[256];
		amiga_machine_info(machine, sizeof(machine));
		std::fprintf(t, "Bug report %s\n\nRelease: %s\nGame: %s (%s data)\nMachine: %s\n", name, gszProductName,
		    gbIsHellfire ? "Hellfire" : "Diablo", gbIsSpawn ? "shareware" : "full", machine);
		std::fprintf(t, "Hero: %s, character level %d; dungeon level %d (type %d%s)\n", MyPlayer->_pName,
		    MyPlayer->getCharacterLevel(), currlevel, static_cast<int>(leveltype), setlevel ? ", quest level" : "");
		std::fprintf(t, "Files: %s.sv (copy of %s), %s.ini, %s-screen.bmp%s\n\n", name, amiga_save_path(), name, name,
		    shot ? "" : " (not written)");
		std::fprintf(t, "What happened (write it here):\n\n");
		std::fclose(t);
	}
	Start(name, base + name, dir, ok);
}

} // namespace devilution
