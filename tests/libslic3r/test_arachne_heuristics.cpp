#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Arachne/WallToolPaths.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"

using namespace Slic3r;

// Helper to create default configs (can be adapted from test_shrinkage.cpp)
PrintConfig মেয়েরা_default_print_config_arachne() {
    PrintConfig config;
    config.apply(FullPrintConfig::defaults());
    config.nozzle_diameter.values = {0.4};
    config.filament_diameter.values = {1.75};
    // Arachne specific defaults often differ, ensure they are set if not covered by FullPrintConfig
    config.line_width = ConfigOptionFloatOrPercent(0.4, false); // Default line width often matches nozzle for Arachne base
    return config;
}

PrintObjectConfig create_default_object_config_arachne(const PrintConfig& print_config_ref) {
    PrintObjectConfig config;
    config.apply(print_config_ref);
    config.layer_height = 0.2;
    config.wall_loops = 3; // Default to a few loops

    config.outer_wall_line_width = ConfigOptionFloatOrPercent(0.40, false);
    config.inner_wall_line_width = ConfigOptionFloatOrPercent(0.40, false);
    // For Arachne, these often default to nozzle diameter or are calculated.
    // Explicitly set to nozzle diameter for these tests.
    config.min_feature_size = ConfigOptionPercent(25, true); // % of nozzle_diameter
    config.min_bead_width = ConfigOptionPercent(85, true);   // % of nozzle_diameter
    config.wall_transition_length = ConfigOptionPercent(100, true);
    config.wall_transition_angle = ConfigOptionFloat(10.0);
    config.wall_transition_filter_deviation = ConfigOptionPercent(25, true);
    config.wall_distribution_count = ConfigOptionInt(1);

    return config;
}

PrintRegionConfig create_default_region_config_arachne(const PrintObjectConfig& object_config_ref) {
    PrintRegionConfig config;
    config.apply(object_config_ref);
    config.wall_generator = PerimeterGeneratorType::Arachne; // Crucial for these tests
    config.arachne_thin_wall_strategy = ArachneThinWallStrategy::awsBalanced; // Default
    return config;
}

// Helper to create a thin wall ExPolygon (a long rectangle)
ExPolygon create_thin_wall_shape(double length, double thickness) {
    Polygon poly;
    poly.points.emplace_back(0, 0);
    poly.points.emplace_back(scale_(length), 0);
    poly.points.emplace_back(scale_(length), scale_(thickness));
    poly.points.emplace_back(0, scale_(thickness));
    return ExPolygon(poly);
}

// Helper to analyze Arachne output for a simple thin wall
struct ArachneWallAnalysis {
    size_t num_lines = 0;
    std::vector<double> average_widths;
    std::vector<double> min_widths;
    std::vector<double> max_widths;

    ArachneWallAnalysis(const ExtrusionEntityCollection& loops_out) {
        for (const ExtrusionEntity* entity : loops_out.entities) {
            // Assuming for a simple thin wall, we get ExtrusionLoop or ExtrusionMultiPath
            // which can be broken down into ExtrusionPaths
            std::vector<const ExtrusionPath*> paths_to_analyze;
            if (const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(entity)) {
                for(const auto& p : loop->paths) paths_to_analyze.push_back(&p);
            } else if (const ExtrusionMultiPath* mpath = dynamic_cast<const ExtrusionMultiPath*>(entity)) {
                 for(const auto& p : mpath->paths) paths_to_analyze.push_back(&p);
            } else if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(entity)){
                paths_to_analyze.push_back(path);
            }

            for (const ExtrusionPath* path : paths_to_analyze) {
                if (path->polyline.points.size() < 2) continue;
                num_lines++; // Counting distinct path segments from Arachne's output structure
                double total_width = 0;
                double current_min_w = 1e9;
                double current_max_w = 0;
                size_t num_junctions = 0; // Arachne paths are series of junctions

                // Need to look into how Arachne::VariableWidthLines and ExtrusionLine store widths
                // This is a simplification. Actual Arachne output is std::vector<VariableWidthLines>
                // For this test, we assume generate_perimeters_for_surface simplifies this for us.
                // The "width" on ExtrusionPath is a single value, not variable.
                // A proper test would inspect the VariableWidthLines from WallToolPaths::getToolPaths()
                total_width += path->width * path->polyline.length(); // Weighted average might be better
                current_min_w = std::min(current_min_w, path->width);
                current_max_w = std::max(current_max_w, path->width);
                num_junctions += path->polyline.points.size(); // Approximation

                if (num_junctions > 0 && path->polyline.length() > 0) {
                    average_widths.push_back(unscale(total_width / path->polyline.length()));
                    min_widths.push_back(unscale(current_min_w));
                    max_widths.push_back(unscale(current_max_w));
                }
            }
        }
    }
};


TEST_CASE("Arachne Thin Wall - Balanced Strategy", "[ArachneHeuristics][Classic]") {
    PrintConfig print_config = create_default_print_config_arachne();
    PrintObjectConfig object_config = create_default_object_config_arachne(print_config);
    PrintRegionConfig region_config = create_default_region_config_arachne(object_config);
    region_config.arachne_thin_wall_strategy = ArachneThinWallStrategy::awsBalanced;

    // Wall thickness 0.7mm. Nominal line width 0.4mm. Min bead width ~0.34mm.
    // Expectation: Arachne might try one wide line, or two lines if it can fit them.
    ExPolygon thin_wall = create_thin_wall_shape(20.0, 0.7);
    ExtrusionEntityCollection loops = generate_perimeters_for_surface(thin_wall, print_config, object_config, region_config);
    ArachneWallAnalysis analysis(loops);

    // These are example assertions and will likely need adjustment based on actual Arachne behavior
    // For 0.7mm wall, Balanced might produce 2 lines if they are not too thin, or one wider line.
    // Let's tentatively expect 2 lines for now, or 1 very wide one.
    CHECK((analysis.num_lines >= 1 && analysis.num_lines <= 2));
    if (analysis.num_lines == 1) {
        REQUIRE(analysis.average_widths[0] == Approx(0.7).margin(0.1));
    } else if (analysis.num_lines == 2) {
        REQUIRE(analysis.average_widths[0] == Approx(0.35).margin(0.05));
        REQUIRE(analysis.average_widths[1] == Approx(0.35).margin(0.05));
    }
}

TEST_CASE("Arachne Thin Wall - Prefer Quality Strategy", "[ArachneHeuristics][Classic]") {
    PrintConfig print_config = create_default_print_config_arachne();
    PrintObjectConfig object_config = create_default_object_config_arachne(print_config);
    PrintRegionConfig region_config = create_default_region_config_arachne(object_config);
    region_config.arachne_thin_wall_strategy = ArachneThinWallStrategy::awsPreferQuality;

    ExPolygon thin_wall = create_thin_wall_shape(20.0, 0.7);
    ExtrusionEntityCollection loops = generate_perimeters_for_surface(thin_wall, print_config, object_config, region_config);
    ArachneWallAnalysis analysis(loops);

    // Expectation: PreferQuality should try to fit two thinner lines if possible.
    // Min bead width is ~0.34. Two such lines need ~0.68 + spacing. 0.7mm wall might just fit two.
    REQUIRE(analysis.num_lines == 2);
    if (analysis.num_lines == 2) {
        REQUIRE(analysis.average_widths[0] < 0.40); // Thinner than nominal
        REQUIRE(analysis.average_widths[1] < 0.40);
        REQUIRE(analysis.min_widths[0] >= unscale(object_config.min_bead_width.get_abs_value(print_config.nozzle_diameter.values[0])) - 0.02); // Close to min_bead_width
    }
}

TEST_CASE("Arachne Thin Wall - Prefer Strength Strategy", "[ArachneHeuristics][Classic]") {
    PrintConfig print_config = create_default_print_config_arachne();
    PrintObjectConfig object_config = create_default_object_config_arachne(print_config);
    PrintRegionConfig region_config = create_default_region_config_arachne(object_config);
    region_config.arachne_thin_wall_strategy = ArachneThinWallStrategy::awsPreferStrength;

    ExPolygon thin_wall = create_thin_wall_shape(20.0, 0.7);
    ExtrusionEntityCollection loops = generate_perimeters_for_surface(thin_wall, print_config, object_config, region_config);
    ArachneWallAnalysis analysis(loops);

    // Expectation: PreferStrength should try to use one thicker line if possible.
    REQUIRE(analysis.num_lines == 1);
    if (analysis.num_lines == 1) {
        REQUIRE(analysis.average_widths[0] > 0.40); // Wider than nominal
        REQUIRE(analysis.average_widths[0] == Approx(0.7).margin(0.05)); // Tries to fill the space
    }
}

// Test for a very thin wall, below min_bead_width but above min_feature_size
TEST_CASE("Arachne Thin Wall - Very Thin Feature", "[ArachneHeuristics][Classic]") {
    PrintConfig print_config = create_default_print_config_arachne();
    PrintObjectConfig object_config = create_default_object_config_arachne(print_config);
    PrintRegionConfig region_config = create_default_region_config_arachne(object_config);

    double min_bead_abs = object_config.min_bead_width.get_abs_value(print_config.nozzle_diameter.values[0]); // e.g. 0.34
    double min_feature_abs = object_config.min_feature_size.get_abs_value(print_config.nozzle_diameter.values[0]); // e.g. 0.1

    ExPolygon very_thin_wall = create_thin_wall_shape(20.0, (min_bead_abs + min_feature_abs) / 2.0 ); // e.g. 0.22mm

    region_config.arachne_thin_wall_strategy = ArachneThinWallStrategy::awsPreferQuality;
    ExtrusionEntityCollection loops_quality = generate_perimeters_for_surface(very_thin_wall, print_config, object_config, region_config);
    ArachneWallAnalysis analysis_quality(loops_quality);
    REQUIRE(analysis_quality.num_lines == 1); // Should still print one line due to WideningBeadingStrategy
    if(analysis_quality.num_lines == 1) REQUIRE(analysis_quality.average_widths[0] == Approx(min_bead_abs).margin(0.02));

    region_config.arachne_thin_wall_strategy = ArachneThinWallStrategy::awsPreferStrength;
    ExtrusionEntityCollection loops_strength = generate_perimeters_for_surface(very_thin_wall, print_config, object_config, region_config);
    ArachneWallAnalysis analysis_strength(loops_strength);
    REQUIRE(analysis_strength.num_lines == 1);
    if(analysis_strength.num_lines == 1) REQUIRE(analysis_strength.average_widths[0] == Approx(min_bead_abs).margin(0.02));
}

TEST_CASE("Arachne Thin Wall - Below Min Feature Size", "[ArachneHeuristics][Classic]") {
    PrintConfig print_config = create_default_print_config_arachne();
    PrintObjectConfig object_config = create_default_object_config_arachne(print_config);
    PrintRegionConfig region_config = create_default_region_config_arachne(object_config);

    double min_feature_abs = object_config.min_feature_size.get_abs_value(print_config.nozzle_diameter.values[0]); // e.g. 0.1

    ExPolygon too_thin_wall = create_thin_wall_shape(20.0, min_feature_abs / 2.0 ); // e.g. 0.05mm

    ExtrusionEntityCollection loops = generate_perimeters_for_surface(too_thin_wall, print_config, object_config, region_config);
    ArachneWallAnalysis analysis(loops);
    REQUIRE(analysis.num_lines == 0); // Should not print anything
}
