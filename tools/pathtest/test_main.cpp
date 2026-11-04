// Equivalence test of FindPath: prints a hash of the paths for random maps.
#include <cstdint>
#include <cstdio>
#include <string_view>
#include "engine/path.h"
namespace devilution {
[[noreturn]] void app_fatal(std::string_view s) { std::printf("FATAL %.*s\n", (int)s.size(), s.data()); std::exit(1); }
}
using namespace devilution;
// the Warp port's path.cpp counts searches for the stats line
extern "C" { unsigned long msdl_counter[4]; unsigned long amiga_eclock(void) { return 0; } }
static uint8_t wall[112][112];
static uint32_t rng = 12345;
static uint32_t R() { rng = rng * 1103515245u + 12345u; return rng >> 8; }
int main()
{
	uint32_t h = 2166136261u;
	long found = 0;
	for (int maxLen : { 100, 25 })
	for (int map = 0; map < 40; map++) {
		int density = 10 + map * 2;
		for (int x = 0; x < 112; x++)
			for (int y = 0; y < 112; y++)
				wall[x][y] = (x == 0 || y == 0 || x == 111 || y == 111 || (int)(R() % 100) < density);
		for (int t = 0; t < 400; t++) {
			Point a { (int)(R() % 110) + 1, (int)(R() % 110) + 1 };
			Point b { a.x + (int)(R() % 61) - 30, a.y + (int)(R() % 61) - 30 };
			if (b.x < 1) b.x = 1; if (b.y < 1) b.y = 1; if (b.x > 110) b.x = 110; if (b.y > 110) b.y = 110;
			int8_t path[100];
			auto posOk = [](Point p) { return p.x >= 0 && p.y >= 0 && p.x < 112 && p.y < 112 && !wall[p.x][p.y]; };
			auto canStep = [](Point s, Point d) { return !wall[s.x][d.y] && !wall[d.x][s.y]; };
			int len = FindPath(canStep, posOk, a, b, path, maxLen);
			if (len > 0) found++;
			h = (h ^ (uint32_t)len) * 16777619u;
			for (int i = 0; i < len; i++) h = (h ^ (uint8_t)path[i]) * 16777619u;
		}
	}
	std::printf("paths found %ld, hash %08x, early no-path %lu\n", found, h, msdl_counter[3]);
	return 0;
}
