//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "BeadingStrategyFactory.hpp"

#include <boost/log/trivial.hpp>
#include <memory>
#include <utility>

#include "LimitedBeadingStrategy.hpp"
#include "WideningBeadingStrategy.hpp"
#include "DistributedBeadingStrategy.hpp"
#include "RedistributeBeadingStrategy.hpp"
#include "OuterWallInsetBeadingStrategy.hpp"
#include "libslic3r/Arachne/BeadingStrategy/BeadingStrategy.hpp"

namespace Slic3r::Arachne {

BeadingStrategyPtr BeadingStrategyFactory::makeStrategy(const coord_t preferred_bead_width_outer,
                                                        const coord_t preferred_bead_width_inner,
                                                        const coord_t preferred_transition_length,
                                                        const float   transitioning_angle,
                                                        const bool    print_thin_walls,
                                                        const coord_t min_bead_width,
                                                        const coord_t min_feature_size,
                                                        const double  wall_split_middle_threshold,
                                                        const double  wall_add_middle_threshold,
                                                        const coord_t max_bead_count,
                                                        const coord_t outer_wall_offset,
                                                        const int     inward_distributed_center_wall_count,
                                                        const double  minimum_variable_line_ratio,
                                                        const ArachneThinWallStrategy thin_wall_strategy,
                                                        const int     layer_id)
{
    // Adjust parameters based on thin_wall_strategy
    double current_wall_split_middle_threshold = wall_split_middle_threshold;
    double current_wall_add_middle_threshold = wall_add_middle_threshold;
    coord_t current_min_bead_width_for_widening = min_bead_width; // This will be used for WideningBeadingStrategy

    if (thin_wall_strategy == ArachneThinWallStrategy::awsPreferQuality) {
        current_wall_split_middle_threshold = std::max(0.1, wall_split_middle_threshold - 0.2);
        current_wall_add_middle_threshold = std::max(0.1, wall_add_middle_threshold - 0.2);
        // For quality, WideningBeadingStrategy might be allowed to use lines closer to the absolute min_bead_width
        // No direct change to current_min_bead_width_for_widening here, but it informs how it's used or if other internal params of WideningStrategy are tweaked.
    } else if (thin_wall_strategy == ArachneThinWallStrategy::awsPreferStrength) {
        current_wall_split_middle_threshold = std::min(0.9, wall_split_middle_threshold + 0.2);
        current_wall_add_middle_threshold = std::min(0.9, wall_add_middle_threshold + 0.2);
        // For strength, WideningBeadingStrategy might be encouraged to make single lines thicker
        // This could mean passing a slightly higher effective min_bead_width to it, or altering its splitting decision.
        // For now, we'll pass the original min_bead_width, but this area could be refined by modifying WideningBeadingStrategy itself.
    }

    // Conditional Logging
    // Define ARACHNE_DECISION_LOGGING via CXXFLAGS for example, or locally for testing.
    // For example: getenv("ARACHNE_DECISION_LOGGING") != nullptr
    #ifdef ARACHNE_DECISION_LOGGING
    if (true) { // Replace 'true' with a runtime check if preferred, e.g., check an environment variable or a debug setting from PrintConfig
        std::string strategy_str = "Balanced";
        if (thin_wall_strategy == ArachneThinWallStrategy::awsPreferQuality) strategy_str = "Quality";
        else if (thin_wall_strategy == ArachneThinWallStrategy::awsPreferStrength) strategy_str = "Strength";

        BOOST_LOG_TRIVIAL(debug) << "ArachneDecisionLog: layer_id=" << layer_id
                                 << ", strategy=" << strategy_str
                                 << ", input_split_thresh=" << wall_split_middle_threshold
                                 << ", adj_split_thresh=" << current_wall_split_middle_threshold
                                 << ", input_add_thresh=" << wall_add_middle_threshold
                                 << ", adj_add_thresh=" << current_wall_add_middle_threshold
                                 << ", input_min_bead_w=" << min_bead_width
                                 << ", adj_min_bead_w_widening=" << current_min_bead_width_for_widening
                                 << ", preferred_outer_w=" << preferred_bead_width_outer
                                 << ", preferred_inner_w=" << preferred_bead_width_inner
                                 << ", min_feature_size=" << min_feature_size;
    }
    #endif

    // Handle a special case when there is just one external perimeter.
    // Because big differences in bead width for inner and other perimeters cause issues with current beading strategies.
    const coord_t      optimal_width = max_bead_count <= 2 ? preferred_bead_width_outer : preferred_bead_width_inner;
    BeadingStrategyPtr ret = std::make_unique<DistributedBeadingStrategy>(optimal_width, preferred_transition_length, transitioning_angle,
                                                                          current_wall_split_middle_threshold, current_wall_add_middle_threshold,
                                                                          inward_distributed_center_wall_count);

    BOOST_LOG_TRIVIAL(trace) << "Applying the Redistribute meta-strategy with outer-wall width = " << preferred_bead_width_outer << ", inner-wall width = " << preferred_bead_width_inner << ".";
    ret = std::make_unique<RedistributeBeadingStrategy>(preferred_bead_width_outer, minimum_variable_line_ratio, std::move(ret));

    if (print_thin_walls) {
        BOOST_LOG_TRIVIAL(trace) << "Applying the Widening Beading meta-strategy with minimum input width " << min_feature_size << " and minimum output width " << current_min_bead_width_for_widening << ", strategy: " << static_cast<int>(thin_wall_strategy);
        // Note: WideningBeadingStrategy itself might need to be aware of thin_wall_strategy to change its internal logic
        // beyond just min_bead_width, or we might need different sub-strategies here.
        // For now, we are only adjusting the thresholds for DistributedBeadingStrategy and passing potentially adjusted min_bead_width.
        ret = std::make_unique<WideningBeadingStrategy>(std::move(ret), min_feature_size, current_min_bead_width_for_widening);
    }
    // Orca: we allow negative outer_wall_offset here
    if (outer_wall_offset != 0) {
        BOOST_LOG_TRIVIAL(trace) << "Applying the OuterWallOffset meta-strategy with offset = " << outer_wall_offset << ".";
        ret = std::make_unique<OuterWallInsetBeadingStrategy>(outer_wall_offset, std::move(ret));
    }

    // Apply the LimitedBeadingStrategy last, since that adds a 0-width marker wall which other beading strategies shouldn't touch.
    BOOST_LOG_TRIVIAL(trace) << "Applying the Limited Beading meta-strategy with maximum bead count = " << max_bead_count << ".";
    ret = std::make_unique<LimitedBeadingStrategy>(max_bead_count, std::move(ret));
    return ret;
}
} // namespace Slic3r::Arachne
