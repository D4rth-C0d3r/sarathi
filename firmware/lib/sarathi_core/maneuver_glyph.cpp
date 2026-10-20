#include "maneuver_glyph.h"

#include <initializer_list>

namespace sarathi::core {
namespace {

void AddStroke(ManeuverGlyph& g, std::initializer_list<GlyphPoint> points) {
  if (g.stroke_count >= g.strokes.size()) {
    return;
  }
  GlyphStroke& s = g.strokes[g.stroke_count++];
  for (const GlyphPoint p : points) {
    if (s.count < s.points.size()) {
      s.points[s.count++] = p;
    }
  }
}

ManeuverGlyph Mirror(ManeuverGlyph g) {
  for (size_t i = 0; i < g.stroke_count; ++i) {
    for (size_t j = 0; j < g.strokes[i].count; ++j) {
      g.strokes[i].points[j].x = static_cast<int8_t>(kGlyphSize - g.strokes[i].points[j].x);
    }
  }
  g.ring_center.x = static_cast<int8_t>(kGlyphSize - g.ring_center.x);
  return g;
}

ManeuverGlyph Ahead() {
  ManeuverGlyph g;
  AddStroke(g, {{50, 95}, {50, 10}});
  AddStroke(g, {{28, 32}, {50, 10}, {72, 32}});
  return g;
}

ManeuverGlyph Right() {
  ManeuverGlyph g;
  AddStroke(g, {{32, 95}, {32, 45}, {86, 45}});
  AddStroke(g, {{64, 23}, {86, 45}, {64, 67}});
  return g;
}

ManeuverGlyph SlightRight() {
  ManeuverGlyph g;
  AddStroke(g, {{40, 95}, {40, 60}, {76, 22}});
  AddStroke(g, {{48, 18}, {76, 22}, {78, 50}});
  return g;
}

ManeuverGlyph SharpRight() {
  ManeuverGlyph g;
  AddStroke(g, {{30, 95}, {30, 22}, {76, 70}});
  AddStroke(g, {{78, 42}, {76, 70}, {48, 72}});
  return g;
}

ManeuverGlyph UTurn() {
  ManeuverGlyph g;
  AddStroke(g, {{30, 95}, {30, 38}, {38, 22}, {50, 16}, {62, 22}, {70, 38}, {70, 72}});
  AddStroke(g, {{52, 54}, {70, 72}, {88, 54}});
  return g;
}

ManeuverGlyph Roundabout(uint8_t exit) {
  ManeuverGlyph g;
  g.has_ring = true;
  g.ring_center = {50, 48};
  g.ring_radius = 18;
  AddStroke(g, {{50, 95}, {50, 66}});  // approach from below
  switch (exit) {
    case 1:  // first exit: right
      AddStroke(g, {{68, 48}, {90, 48}});
      AddStroke(g, {{76, 34}, {90, 48}, {76, 62}});
      break;
    case 3:  // third exit: left
      AddStroke(g, {{32, 48}, {10, 48}});
      AddStroke(g, {{24, 34}, {10, 48}, {24, 62}});
      break;
    case 2:   // second exit: straight on
    default:  // other exits: shown as straight on with the number in text
      AddStroke(g, {{50, 30}, {50, 6}});
      AddStroke(g, {{36, 20}, {50, 6}, {64, 20}});
      break;
  }
  return g;
}

ManeuverGlyph Arrive() {
  ManeuverGlyph g;
  AddStroke(g, {{36, 95}, {36, 8}});
  AddStroke(g, {{36, 8}, {80, 22}, {36, 36}});
  return g;
}

}  // namespace

ManeuverGlyph GlyphFor(const Maneuver& maneuver) {
  switch (maneuver.type) {
    case ManeuverType::kDepart:
    case ManeuverType::kStraight:
      return Ahead();
    case ManeuverType::kRight:
      return Right();
    case ManeuverType::kLeft:
      return Mirror(Right());
    case ManeuverType::kSlightRight:
      return SlightRight();
    case ManeuverType::kSlightLeft:
      return Mirror(SlightRight());
    case ManeuverType::kSharpRight:
      return SharpRight();
    case ManeuverType::kSharpLeft:
      return Mirror(SharpRight());
    case ManeuverType::kUTurn:
      return UTurn();
    case ManeuverType::kRoundabout:
      return Roundabout(maneuver.roundabout_exit);
    case ManeuverType::kArrive:
      return Arrive();
    case ManeuverType::kNone:
      break;
  }
  return {};
}

}  // namespace sarathi::core
