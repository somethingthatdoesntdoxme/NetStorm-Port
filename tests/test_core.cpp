// Data-free unit tests (run in CI, where there's no NetStorm install):
// mission script parsing, GIF decoding, island tiles.
#include <cstdio>
#include <string>

#include "data/Gif.h"
#include "data/MissionInfo.h"
#include "world/Island.h"

using namespace ns;

static int fails = 0;
#define CHECK(c, msg)                     \
  do {                                    \
    if (!(c)) {                           \
      std::printf("FAIL: %s\n", msg);    \
      ++fails;                            \
    } else {                              \
      std::printf("ok:   %s\n", msg);    \
    }                                     \
  } while (0)

static void missionScript() {
  // A made-up script in the original's format.
  const char* text =
      "[Header]\n"
      "title = Test Mission\n"
      "myStartMoney = 1500\n"
      "myTech = sunArcher,sunCannon\n"
      "ai2Name = Test Baron\n"
      "ai2Tech = sunArcher\n"
      "ai2StartMoney = 900\n"
      "ai2Color = red\n"
      "[A.]\n"
      "Test Mission\n\nTake the island.\n"
      "$Button=Begin,MissionBegin,Next\n"
      "[Succeeded][BadTeamDead]\n"
      "Success! Well done.\n"
      "$Button=Read the note,Tell,Note\n"
      "[Note]\n"
      "A note from the Baron.\n"
      "$Button=Next Mission,MissionBegin,NextOne\n"
      "[Failed]\n"
      "Failure!\n"
      "[@120]\n"
      "Two minutes in.\n"
      "$Button=Surrender,Tell,Surrender\n"
      "[Failed][Ai2PriestDead]\n"
      "You let him die.\n";
  MissionInfo m = MissionInfo::parse(text, "testmission");
  CHECK(m.title == "Test Mission", "title from the header");
  CHECK(m.next == "nextone", "next mission found by following Tell hops from [Succeeded]");
  CHECK(m.success.find("note from the Baron") != std::string::npos, "success text includes the Tell'd note");
  CHECK(m.players[2].present && m.players[2].name == "Test Baron", "AI player header");
  CHECK(m.players[2].startMoney == 900, "AI start money");
  const ScriptSection* timed = m.section("@120");
  CHECK(timed && timed->buttons.size() == 1 && timed->buttons[0].action == "tell", "timed note with a Tell button");
  const ScriptSection* combo = m.section("ai2priestdead");
  bool hasFailed = false;
  if (combo)
    for (auto& k : combo->keys) hasFailed |= k == "failed";
  CHECK(combo && hasFailed, "multi-key header keeps every key");
  std::string plain = missionTextToPlain("<b>Bold</b> &amp; ~oplain");
  std::printf("      plain text: [%s]\n", plain.c_str());
  CHECK(plain.find('<') == std::string::npos && plain.find('~') == std::string::npos, "markup and ~ codes stripped");
  CHECK(plain.find("&amp;") == std::string::npos, "HTML entities decoded");
}

static void gif() {
  // Hand-built 1x1 GIF89a: 2-colour palette (red, black), one red pixel.
  const unsigned char bytes[] = {0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
                                 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                                 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};
  Bytes b(bytes, bytes + sizeof bytes);
  auto img = decodeGif(b);
  CHECK(img && img->w == 1 && img->h == 1, "1x1 GIF decodes");
  CHECK(img && img->px.size() == 4 && img->px[0] == 255 && img->px[1] == 0 && img->px[3] == 255, "pixel is opaque red");
  Bytes junk{'G', 'I', 'F', '8'};
  CHECK(!decodeGif(junk), "truncated GIF rejected, no crash");
}

static void island() {
  Island w(10, 10);
  CHECK(!w.filled(3, 3), "new island is empty sky");
  w.set(3, 3, Element::Rain);
  w.setOwner(3, 3, 2);
  CHECK(w.filled(3, 3) && w.ownerAt(3, 3) == 2, "set land and owner");
  CHECK(!w.filled(-1, 3) && !w.filled(10, 3), "out-of-bounds reads are sky, not a crash");
}

int main() {
  missionScript();
  gif();
  island();
  std::printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
  return fails ? 1 : 0;
}
