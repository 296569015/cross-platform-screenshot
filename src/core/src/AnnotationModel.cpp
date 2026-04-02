#include "core/AnnotationModel.h"

namespace sst::core {

void AnnotationModel::addAnnotation(Annotation ann) {
    annotations_.push_back(std::move(ann));
}

void AnnotationModel::removeAnnotation(size_t index) {
    if (index < annotations_.size()) {
        annotations_.erase(annotations_.begin() + static_cast<ptrdiff_t>(index));
    }
}

void AnnotationModel::clear() {
    annotations_.clear();
}

} // namespace sst::core
