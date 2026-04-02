#pragma once

#include <platform/PlatformTypes.h>
#include <string>
#include <variant>
#include <vector>

namespace sst::core {

/// Annotation types that users can draw on screenshots.
struct RectAnnotation {
    platform::RectF bounds;
    platform::Color color;
    float           thickness = 2.0f;
    bool            filled    = false;
};

struct ArrowAnnotation {
    platform::PointF start;
    platform::PointF end;
    platform::Color  color;
    float            thickness = 2.0f;
    float            headSize  = 12.0f;
};

struct TextAnnotation {
    platform::PointF position;
    std::string      text;
    platform::Color  color;
    float            fontSize = 16.0f;
};

struct LineAnnotation {
    platform::PointF start;
    platform::PointF end;
    platform::Color  color;
    float            thickness = 2.0f;
};

/// A single annotation (any type).
using Annotation = std::variant<
    RectAnnotation,
    ArrowAnnotation,
    TextAnnotation,
    LineAnnotation
>;

/// The active annotation tool.
enum class AnnotationTool {
    None,
    Rectangle,
    Arrow,
    Text,
    Line,
};

} // namespace sst::core
