#pragma once

#include "Types.h"
#include <vector>

namespace sst::core {

/// Manages the list of annotations on the current screenshot.
class AnnotationModel {
public:
    void addAnnotation(Annotation ann);
    void removeAnnotation(size_t index);
    void clear();

    const std::vector<Annotation>& annotations() const { return annotations_; }
    size_t count() const { return annotations_.size(); }

private:
    std::vector<Annotation> annotations_;
};

} // namespace sst::core
