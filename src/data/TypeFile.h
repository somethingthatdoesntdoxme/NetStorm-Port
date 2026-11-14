#pragma once
// Parser for the original "*.type" scripts (decrypted from netstorm.tarc).
//
//   typename windArcher constructor
//   typeflags createsisland emplacement shadow saveFrame;
//   {
//       description = "Crossbow";
//       maxHitPoints = 480;
//   }
//   L00 : default : "warch1.gif" #00 : "warch1a.gif" #71;
//
// Frame lines: a cluster letter group (A..Z, AA..) + 2-digit variation,
// an optional label (default / cracked / hard ...), then one or more
// image references "file.gif" #frame (the 2nd one is usually the shadow).
#include <map>
#include <string>
#include <vector>

namespace ns {

struct ImageRef {
  std::string gif;  // lower-cased
  int frame = 0;
};

struct FrameLine {
  std::string cluster;  // "L", "AA"
  int variation = 0;    // 0..99
  std::string label;
  std::vector<ImageRef> images;
};

struct TypeDef {
  std::string file;
  std::string name;
  std::vector<std::string> nameWords;  // words after the type name (constructor, client...)
  std::vector<std::string> flags;
  std::map<std::string, std::string> props;  // raw values, quotes stripped
  std::vector<FrameLine> frames;

  bool hasFlag(const std::string& f) const;
  std::string str(const std::string& k, const std::string& def = "") const;
  double num(const std::string& k, double def = 0) const;
};

TypeDef parseTypeFile(const std::string& text, const std::string& fileName);

}  // namespace ns
