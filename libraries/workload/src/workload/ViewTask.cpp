//
//  ViewTask.cpp
//  libraries/workload/src/workload
//
//  Created by Sam Gateau 2018.03.05
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "ViewTask.h"

using namespace workload;


void AssignSpaceViews::run(const WorkloadContextPointer& renderContext, const Input& inputs) {
    // Just do what it says
    renderContext->_space->setViews(inputs);
}

void SetupViews::configure(const Config& config) {
    data = config.data;
}

void SetupViews::run(const WorkloadContextPointer& renderContext, const Input& inputs, Output& outputs) {
    // If views are frozen don't use the input
    if (!data.freezeViews) {
        _views = inputs;
    }

    auto& outViews = outputs;
    outViews.clear();

    // Filter the first view centerer on the avatar head if needed
    if (_views.size() >= 2) {
        if (data.useAvatarView) {
            outViews.push_back(_views[0]);
            outViews.insert(outViews.end(), _views.begin() + 2, _views.end());
        } else {
            outViews.insert(outViews.end(), _views.begin() + 1, _views.end());
        }
    } else {
        outViews = _views;
    }

    // Force frutum orientation horizontal if needed
    if (outViews.size() > 0 && data.forceViewHorizontal) {
        outViews[0].makeHorizontal();
    }

    // Force frutum orientation horizontal if needed
    if (outViews.size() > 0 && data.simulateSecondaryCamera) {
        auto view = outViews[0];
        auto secondaryDirectionFlat = glm::normalize(glm::vec3(view.direction.x, 0.0f, view.direction.z));
        auto secondaryDirection = glm::normalize(glm::vec3(secondaryDirectionFlat.z, 0.0f, -secondaryDirectionFlat.x));

        view.origin += -20.0f * secondaryDirection;
        view.direction = -secondaryDirection;

        outViews.insert(outViews.begin() + 1, view);
    }

    // Update regions based on the current config
    for (auto& v : outViews) {
        View::updateRegionsFromBackFrontDistances(v, (float*) &data);
    }

    // outViews is ready to be used
}


ControlViews::ControlViews() {
    for (int32_t i = 0; i < workload::Region::NUM_VIEW_REGIONS; i++) {
        regionBackFronts[i] = MIN_VIEW_BACK_FRONTS[i];
        regionRegulators[i] = Regulator(std::chrono::milliseconds(2), MIN_VIEW_BACK_FRONTS[i], MAX_VIEW_BACK_FRONTS[i], glm::vec2(RELATIVE_STEP_DOWN), glm::vec2(RELATIVE_STEP_UP));
    }
}

void ControlViews::configure(const Config& config) {
    _data = config.data;
}

void ControlViews::run(const workload::WorkloadContextPointer& runContext, const Input& inputs, Output& outputs) {
    const auto& inViews = inputs.get0();
    const auto& inTimings = inputs.get1();
    auto& outViews = outputs;
    outViews.clear();
    outViews = inViews;

    if (_data.regulateViewRanges && inTimings.size()) {
        regulateViews(outViews, inTimings);
    }

    // Export the ranges for debuging
    bool doExport = false;
    if (outViews.size()) {
        _dataExport.ranges[workload::Region::R1] = outViews[0].regionBackFronts[workload::Region::R1];
        _dataExport.ranges[workload::Region::R2] = outViews[0].regionBackFronts[workload::Region::R2];
        _dataExport.ranges[workload::Region::R3] = outViews[0].regionBackFronts[workload::Region::R3];
        doExport = true;
    }

    // Export the ranges and timings for debuging
    if (inTimings.size()) {
        _dataExport.timings[workload::Region::R1] = std::chrono::duration<float, std::milli>(inTimings[0]).count();
        _dataExport.timings[workload::Region::R2] = _dataExport.timings[workload::Region::R1];
        _dataExport.timings[workload::Region::R3] = std::chrono::duration<float, std::milli>(inTimings[1]).count();
        doExport = true;
    }

    if (doExport) {
        auto config = std::static_pointer_cast<Config>(runContext->jobConfig);
        config->dataExport = _dataExport;
        config->emitDirty();
    }
}

glm::vec2 Regulator::run(const Timing_ns& regulationDuration, const Timing_ns& measured, const glm::vec2& current) {
    // Regulate next value based on current moving toward the goal budget
    float error_ms = std::chrono::duration<float, std::milli>(_budget - measured).count();
    float coef = glm::clamp(error_ms / std::chrono::duration<float, std::milli>(regulationDuration).count(), -1.0f, 1.0f);
    return current * (1.0f + coef * (error_ms < 0.0f ? _relativeStepDown : _relativeStepUp));
}

glm::vec2 Regulator::clamp(const glm::vec2& backFront) const {
    // Clamp to min max
    return glm::clamp(backFront, _minRange, _maxRange);
}

void ControlViews::regulateViews(workload::Views& outViews, const workload::Timings& timings) {

    for (auto& outView : outViews) {
        for (int32_t r = 0; r < workload::Region::NUM_VIEW_REGIONS; r++) {
            outView.regionBackFronts[r] = regionBackFronts[r];
        }
    }

    auto loopDuration = std::chrono::nanoseconds{ std::chrono::milliseconds(16) };
    regionBackFronts[workload::Region::R1] = regionRegulators[workload::Region::R1].run(loopDuration, timings[0], regionBackFronts[workload::Region::R1]);
    regionBackFronts[workload::Region::R2] = regionRegulators[workload::Region::R2].run(loopDuration, timings[0], regionBackFronts[workload::Region::R2]);
    regionBackFronts[workload::Region::R3] = regionRegulators[workload::Region::R3].run(loopDuration, timings[1], regionBackFronts[workload::Region::R3]);

    enforceRegionContainment();
    for (auto& outView : outViews) {
        outView.regionBackFronts[workload::Region::R1] = regionBackFronts[workload::Region::R1];
        outView.regionBackFronts[workload::Region::R2] = regionBackFronts[workload::Region::R2];
        outView.regionBackFronts[workload::Region::R3] = regionBackFronts[workload::Region::R3];

        workload::View::updateRegionsFromBackFronts(outView);
    }
}

void ControlViews::enforceRegionContainment() {
    // inner regions should never overreach outer
    // and each region should never exceed its min/max limits
    const glm::vec2 MIN_REGION_GAP = { 1.0f, 2.0f };
    // enforce outside --> in
    for (int32_t i = workload::Region::NUM_VIEW_REGIONS - 2; i >= 0; --i) {
        int32_t j = i + 1;
        regionBackFronts[i] = regionRegulators[i].clamp(glm::min(regionBackFronts[i], regionBackFronts[j] - MIN_REGION_GAP));
    }
    // enforce inside --> out
    for (int32_t i = 1; i < workload::Region::NUM_VIEW_REGIONS; ++i) {
        int32_t j = i - 1;
        regionBackFronts[i] = regionRegulators[i].clamp(glm::max(regionBackFronts[i], regionBackFronts[j] + MIN_REGION_GAP));
    }
}
