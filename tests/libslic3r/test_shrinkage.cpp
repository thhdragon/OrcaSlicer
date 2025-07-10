#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/SurfaceCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"

using namespace Slic3r;

// Helper to create default configs
PrintConfig create_default_print_config() {
    PrintConfig config;
    config.apply(FullPrintConfig::defaults()); // Apply general defaults first
    config.nozzle_diameter.values = {0.4};
    config.filament_diameter.values = {1.75};
    config.filament_shrink.values = {100.0};
    config.filament_shrinkage_compensation_z.values = {100.0};
    return config;
}

PrintObjectConfig create_default_object_config(const PrintConfig& print_config_ref) {
    PrintObjectConfig config;
    config.apply(print_config_ref); // Apply from print_config, not FullPrintConfig directly
    config.layer_height = 0.2;
    config.wall_loops = 2;

    // Ensure line width settings are initialized properly
    config.line_width = ConfigOptionFloatOrPercent(0.45, false);
    config.outer_wall_line_width = ConfigOptionFloatOrPercent(0.40, false); // Explicitly different for testing
    config.inner_wall_line_width = ConfigOptionFloatOrPercent(0.45, false);

    config.outer_wall_shrinkage_xy = ConfigOptionPercent(100.0);
    config.inner_wall_shrinkage_xy = ConfigOptionPercent(100.0);
    config.hole_shrinkage_xy = ConfigOptionPercent(100.0);
    config.xy_hole_compensation = ConfigOptionFloat(0.0);
    config.xy_contour_compensation = ConfigOptionFloat(0.0);
    return config;
}

PrintRegionConfig create_default_region_config(const PrintObjectConfig& object_config_ref) {
    PrintRegionConfig config;
    config.apply(object_config_ref); // Apply from object_config
    return config;
}


// Helper to run perimeter generator (simplified)
ExtrusionEntityCollection generate_perimeters_for_surface(const ExPolygon& surface_shape,
                                                          const PrintConfig& print_config,
                                                          const PrintObjectConfig& object_config,
                                                          const PrintRegionConfig& region_config,
                                                          int layer_id = 0,
                                                          const ExPolygons* lower_layers = nullptr) { // Added lower_layers
    SurfaceCollection sc;
    sc.surfaces.emplace_back(stInternal, surface_shape);

    Flow perimeter_flow = Flow::new_from_config_width(frPerimeter, region_config.inner_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);
    Flow ext_perimeter_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);

    ExtrusionEntityCollection loops_out;
    ExtrusionEntityCollection gap_fill_out;
    SurfaceCollection fill_surfaces_out;
    ExPolygons fill_no_overlap_out;

    PerimeterGenerator pg(
        &sc,
        nullptr,
        object_config.layer_height,
        coordf_t(object_config.layer_height * (layer_id + 1)),
        perimeter_flow,
        &region_config,
        &object_config,
        &print_config,
        false,
        &loops_out,
        &gap_fill_out,
        &fill_surfaces_out,
        &fill_no_overlap_out
    );
    pg.ext_perimeter_flow = ext_perimeter_flow;
    pg.overhang_flow = ext_perimeter_flow;
    pg.solid_infill_flow = perimeter_flow;
    pg.layer_id = layer_id;
    if (lower_layers) { // Pass lower layers if provided
        pg.lower_slices = lower_layers;
    }


    if (region_config.wall_generator == PerimeterGeneratorType::Classic) {
        pg.process_classic();
    } else {
        pg.process_arachne();
    }
    return loops_out;
}

Polygons get_polygons_from_entities(const ExtrusionEntityCollection& entities) {
    Polygons polys;
    for (const ExtrusionEntity* entity : entities.entities) {
        if (const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(entity)) {
            polys.push_back(loop->polygon());
        } else if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(entity)) {
            if (path->polyline.is_closed()) {
                 polys.emplace_back(path->polyline.points);
            }
        } else if (const ExtrusionMultiPath* mpath = dynamic_cast<const ExtrusionMultiPath*>(entity)) {
             if (mpath->is_loop()) {
                 polys.push_back(mpath->as_polyline().to_polygon());
             }
        }
    }
    return polys;
}

// Helper to get a specific perimeter (0 = outermost CCW, 1 = next CW (hole) or next CCW (inner))
Polygon get_perimeter_by_index_and_type(const Polygons& perimeters, int target_idx, bool is_hole_type) {
    std::vector<Polygon> sorted_perims = perimeters;
    std::sort(sorted_perims.begin(), sorted_perims.end(), [](const Polygon& a, const Polygon& b) {
        return std::abs(a.area()) > std::abs(b.area()); // Largest area is outermost
    });

    int current_idx = -1;
    for (const auto& p : sorted_perims) {
        bool poly_is_hole = p.is_clockwise(); // Assuming CW for holes, CCW for contours after orientation
        if (is_hole_type == poly_is_hole) {
            current_idx++;
            if (current_idx == target_idx) {
                return p;
            }
        }
    }
    return Polygon(); // Return empty if not found
}


TEST_CASE("No Shrinkage Applied (All 100%)", "[Shrinkage][Classic]") {
    PrintConfig print_config = create_default_print_config();
    PrintObjectConfig object_config = create_default_object_config(print_config);
    PrintRegionConfig region_config = create_default_region_config(object_config);
    region_config.wall_generator = PerimeterGeneratorType::Classic;

    Polygon square_poly;
    square_poly.points.emplace_back(scale_(-50.0), scale_(-50.0));
    square_poly.points.emplace_back(scale_(50.0), scale_(-50.0));
    square_poly.points.emplace_back(scale_(50.0), scale_(50.0));
    square_poly.points.emplace_back(scale_(-50.0), scale_(50.0));
    ExPolygon square_ex_poly(square_poly);

    ExtrusionEntityCollection loops = generate_perimeters_for_surface(square_ex_poly, print_config, object_config, region_config);
    Polygons perimeters = get_polygons_from_entities(loops);

    REQUIRE(perimeters.size() == 2);

    Flow ext_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);
    Flow int_flow = Flow::new_from_config_width(frPerimeter, region_config.inner_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);

    Polygon outer_p = get_perimeter_by_index_and_type(perimeters, 0, false); // Outermost CCW
    Polygon inner_p = get_perimeter_by_index_and_type(perimeters, 1, false); // Next CCW (first inner)

    REQUIRE_FALSE(outer_p.points.empty());
    REQUIRE_FALSE(inner_p.points.empty());

    BoundingBox bbox_outer = get_extents(outer_p);
    REQUIRE(unscale(bbox_outer.max.x()) == Approx(50.0 - ext_flow.width()/2.0).margin(0.015));
    REQUIRE(unscale(bbox_outer.min.x()) == Approx(-50.0 + ext_flow.width()/2.0).margin(0.015));

    BoundingBox bbox_inner = get_extents(inner_p);
    double expected_inner_centerline = 50.0 - ext_flow.width()/2.0 - (0.5 * (ext_flow.spacing() + int_flow.spacing()));
    REQUIRE(unscale(bbox_inner.max.x()) == Approx(expected_inner_centerline).margin(0.015));
}

TEST_CASE("Global XY Shrinkage Only (Classic)", "[Shrinkage][Classic]") {
    PrintConfig print_config = create_default_print_config();
    print_config.filament_shrink.values[0] = 99.0;

    PrintObjectConfig object_config = create_default_object_config(print_config);
    PrintRegionConfig region_config = create_default_region_config(object_config);
    region_config.wall_generator = PerimeterGeneratorType::Classic;

    Polygon square_poly; /* As above */
    square_poly.points.emplace_back(scale_(-50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(50.0)); square_poly.points.emplace_back(scale_(-50.0), scale_(50.0));
    ExPolygon square_ex_poly_orig(square_poly);

    ExPolygon square_ex_poly_globally_shrunk = square_ex_poly_orig;
    square_ex_poly_globally_shrunk.scale(0.99);

    ExtrusionEntityCollection loops = generate_perimeters_for_surface(square_ex_poly_globally_shrunk, print_config, object_config, region_config);
    Polygons perimeters = get_polygons_from_entities(loops);

    REQUIRE(perimeters.size() == 2);
    Polygon outer_p = get_perimeter_by_index_and_type(perimeters, 0, false);
    REQUIRE_FALSE(outer_p.points.empty());

    Flow ext_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);
    double globally_shrunk_coord = 50.0 * 0.99;

    BoundingBox bbox_outer = get_extents(outer_p);
    REQUIRE(unscale(bbox_outer.max.x()) == Approx(globally_shrunk_coord - ext_flow.width()/2.0).margin(0.015));
    REQUIRE(unscale(bbox_outer.min.x()) == Approx(-globally_shrunk_coord + ext_flow.width()/2.0).margin(0.015));
}


TEST_CASE("Outer Wall XY Shrinkage Override (Classic)", "[Shrinkage][Classic]") {
    PrintConfig print_config = create_default_print_config();
    print_config.filament_shrink.values[0] = 99.0;

    PrintObjectConfig object_config = create_default_object_config(print_config);
    object_config.outer_wall_shrinkage_xy = ConfigOptionPercent(98.0);

    PrintRegionConfig region_config = create_default_region_config(object_config);
    region_config.wall_generator = PerimeterGeneratorType::Classic;

    Polygon square_poly; /* As above */
    square_poly.points.emplace_back(scale_(-50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(50.0)); square_poly.points.emplace_back(scale_(-50.0), scale_(50.0));
    ExPolygon square_ex_poly_orig(square_poly);

    ExPolygon square_ex_poly_globally_shrunk = square_ex_poly_orig;
    square_ex_poly_globally_shrunk.scale(0.99);

    ExtrusionEntityCollection loops = generate_perimeters_for_surface(square_ex_poly_globally_shrunk, print_config, object_config, region_config);
    Polygons perimeters = get_polygons_from_entities(loops);

    REQUIRE(perimeters.size() == 2);
    Polygon outer_p = get_perimeter_by_index_and_type(perimeters, 0, false);
    Polygon inner_p = get_perimeter_by_index_and_type(perimeters, 1, false);
    REQUIRE_FALSE(outer_p.points.empty());
    REQUIRE_FALSE(inner_p.points.empty());

    Flow ext_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);
    Flow int_flow = Flow::new_from_config_width(frPerimeter, region_config.inner_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);

    double target_outer_edge = 50.0 * 0.98;
    BoundingBox bbox_outer = get_extents(outer_p);
    REQUIRE(unscale(bbox_outer.max.x()) == Approx(target_outer_edge - ext_flow.width()/2.0).margin(0.02)); // Increased margin for multi-scaling

    double globally_shrunk_outer_centerline = (50.0 * 0.99) - ext_flow.width()/2.0;
    double expected_inner_centerline = globally_shrunk_outer_centerline - (0.5 * (ext_flow.spacing() + int_flow.spacing()));
    BoundingBox bbox_inner = get_extents(inner_p);
    REQUIRE(unscale(bbox_inner.max.x()) == Approx(expected_inner_centerline).margin(0.02));
}

TEST_CASE("Inner Wall XY Shrinkage Override (Classic)", "[Shrinkage][Classic]") {
    PrintConfig print_config = create_default_print_config();
    print_config.filament_shrink.values[0] = 99.0;

    PrintObjectConfig object_config = create_default_object_config(print_config);
    object_config.wall_loops = 3;
    object_config.inner_wall_shrinkage_xy = ConfigOptionPercent(98.5);

    PrintRegionConfig region_config = create_default_region_config(object_config);
    region_config.wall_generator = PerimeterGeneratorType::Classic;

    Polygon square_poly; /* As above */
    square_poly.points.emplace_back(scale_(-50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(50.0)); square_poly.points.emplace_back(scale_(-50.0), scale_(50.0));
    ExPolygon square_ex_poly_orig(square_poly);

    ExPolygon square_ex_poly_globally_shrunk = square_ex_poly_orig;
    square_ex_poly_globally_shrunk.scale(0.99);

    ExtrusionEntityCollection loops = generate_perimeters_for_surface(square_ex_poly_globally_shrunk, print_config, object_config, region_config);
    Polygons perimeters = get_polygons_from_entities(loops);

    REQUIRE(perimeters.size() == (unsigned int)object_config.wall_loops.value);

    Polygon outer_p = get_perimeter_by_index_and_type(perimeters, 0, false);
    Polygon inner1_p = get_perimeter_by_index_and_type(perimeters, 1, false);
    Polygon inner2_p = get_perimeter_by_index_and_type(perimeters, 2, false);
    REQUIRE_FALSE(outer_p.points.empty());
    REQUIRE_FALSE(inner1_p.points.empty());
    REQUIRE_FALSE(inner2_p.points.empty());

    Flow ext_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);
    Flow int_flow = Flow::new_from_config_width(frPerimeter, region_config.inner_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);

    BoundingBox bbox_outer = get_extents(outer_p);
    REQUIRE(unscale(bbox_outer.max.x()) == Approx((50.0 * 0.99) - ext_flow.width()/2.0).margin(0.02));

    double original_inner1_centerline = (50.0 - ext_flow.width()/2.0 - 0.5 * (ext_flow.spacing() + int_flow.spacing()));
    double target_inner1_centerline = original_inner1_centerline * 0.985;
    BoundingBox bbox_inner1 = get_extents(inner1_p);
    REQUIRE(unscale(bbox_inner1.max.x()) == Approx(target_inner1_centerline).margin(0.03));

    double original_inner2_centerline = original_inner1_centerline - int_flow.spacing();
    double target_inner2_centerline = original_inner2_centerline * 0.985;
    BoundingBox bbox_inner2 = get_extents(inner2_p);
    REQUIRE(unscale(bbox_inner2.max.x()) == Approx(target_inner2_centerline).margin(0.04));
}


TEST_CASE("Hole XY Shrinkage Override (Classic)", "[Shrinkage][Classic]") {
    PrintConfig print_config = create_default_print_config();
    print_config.filament_shrink.values[0] = 99.0;

    PrintObjectConfig object_config = create_default_object_config(print_config);
    object_config.hole_shrinkage_xy = ConfigOptionPercent(101.0);

    PrintRegionConfig region_config = create_default_region_config(object_config);
    region_config.wall_generator = PerimeterGeneratorType::Classic;

    Polygon square_poly;
    square_poly.points.emplace_back(scale_(-50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(50.0)); square_poly.points.emplace_back(scale_(-50.0), scale_(50.0));

    Polygon hole_poly_orig;
    for (int i = 0; i < 32; ++i) { hole_poly_orig.points.emplace_back(scale_(10.0 * cos(2 * PI * i / 32.0)), scale_(10.0 * sin(2 * PI * i / 32.0))); }
    hole_poly_orig.reverse();

    ExPolygon square_with_hole_orig(square_poly, {hole_poly_orig});
    ExPolygon square_with_hole_globally_shrunk = square_with_hole_orig;
    square_with_hole_globally_shrunk.scale(0.99);

    ExtrusionEntityCollection loops_collection = generate_perimeters_for_surface(square_with_hole_globally_shrunk, print_config, object_config, region_config);

    Polygon actual_hole_perimeter = get_perimeter_by_index_and_type(get_polygons_from_entities(loops_collection), 0, true); // First hole perimeter (outermost of hole)
    REQUIRE_FALSE(actual_hole_perimeter.points.empty());
    BoundingBox bbox_hole = get_extents(actual_hole_perimeter);

    Flow hole_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height); // Holes are treated like external perimeters

    // Hole: Original radius 10.0. Target hole radius after all scaling: 10.0 * 1.01 = 10.1
    // The perimeter generated is the outermost one for the hole, its centerline is hole_radius + W/2.
    REQUIRE(unscale(bbox_hole.max.x()) == Approx(10.0 * 1.01 + hole_flow.width()/2.0).margin(0.03));
}


TEST_CASE("Feature Specific Shrinkage Default (100%) - Fallback to Global (Classic)", "[Shrinkage][Classic]") {
    PrintConfig print_config = create_default_print_config();
    print_config.filament_shrink.values[0] = 98.5;

    PrintObjectConfig object_config = create_default_object_config(print_config);
    object_config.outer_wall_shrinkage_xy = ConfigOptionPercent(100.0);
    object_config.hole_shrinkage_xy = ConfigOptionPercent(100.0);
    object_config.inner_wall_shrinkage_xy = ConfigOptionPercent(100.0);

    PrintRegionConfig region_config = create_default_region_config(object_config);
    region_config.wall_generator = PerimeterGeneratorType::Classic;

    Polygon square_poly; /* As above */
    square_poly.points.emplace_back(scale_(-50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(50.0)); square_poly.points.emplace_back(scale_(-50.0), scale_(50.0));
    Polygon hole_poly_orig; /* As above */
    for (int i = 0; i < 32; ++i) { hole_poly_orig.points.emplace_back(scale_(10.0 * cos(2*PI*i/32.0)), scale_(10.0 * sin(2*PI*i/32.0))); } hole_poly_orig.reverse();

    ExPolygon square_with_hole_orig(square_poly, {hole_poly_orig});
    ExPolygon square_with_hole_globally_shrunk = square_with_hole_orig;
    square_with_hole_globally_shrunk.scale(0.985);

    ExtrusionEntityCollection loops = generate_perimeters_for_surface(square_with_hole_globally_shrunk, print_config, object_config, region_config);
    Polygons perimeters = get_polygons_from_entities(loops);
    REQUIRE(perimeters.size() == 4); // 2 for outer, 2 for hole

    Flow ext_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);

    Polygon actual_outer_perimeter = get_perimeter_by_index_and_type(perimeters, 0, false);
    BoundingBox bbox_outer = get_extents(actual_outer_perimeter);
    REQUIRE(unscale(bbox_outer.max.x()) == Approx((50.0 * 0.985) - ext_flow.width()/2.0).margin(0.02));

    Polygon actual_hole_perimeter = get_perimeter_by_index_and_type(perimeters, 0, true);
    BoundingBox bbox_hole = get_extents(actual_hole_perimeter);
    REQUIRE(unscale(bbox_hole.max.x()) == Approx((10.0 * 0.985) + ext_flow.width()/2.0).margin(0.025));
}

TEST_CASE("Interaction with xy_hole_compensation (absolute offset) (Classic)", "[Shrinkage][Classic]") {
    PrintConfig print_config = create_default_print_config();
    print_config.filament_shrink.values[0] = 99.0;

    PrintObjectConfig object_config = create_default_object_config(print_config);
    object_config.hole_shrinkage_xy = ConfigOptionPercent(101.0);
    object_config.xy_hole_compensation = ConfigOptionFloat(0.1); // Makes hole radius 0.1mm LARGER

    PrintRegionConfig region_config = create_default_region_config(object_config);
    region_config.wall_generator = PerimeterGeneratorType::Classic;

    Polygon square_poly; /* As above */
    square_poly.points.emplace_back(scale_(-50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(-50.0)); square_poly.points.emplace_back(scale_(50.0), scale_(50.0)); square_poly.points.emplace_back(scale_(-50.0), scale_(50.0));
    Polygon hole_poly_orig; /* As above */
    for (int i = 0; i < 32; ++i) { hole_poly_orig.points.emplace_back(scale_(10.0 * cos(2*PI*i/32.0)), scale_(10.0 * sin(2*PI*i/32.0))); } hole_poly_orig.reverse();

    ExPolygon square_with_hole_orig(square_poly, {hole_poly_orig});
    ExPolygon square_with_hole_globally_shrunk = square_with_hole_orig;
    square_with_hole_globally_shrunk.scale(0.99);

    ExtrusionEntityCollection loops = generate_perimeters_for_surface(square_with_hole_globally_shrunk, print_config, object_config, region_config);

    Polygon actual_hole_perimeter = get_perimeter_by_index_and_type(get_polygons_from_entities(loops), 0, true);
    REQUIRE_FALSE(actual_hole_perimeter.points.empty());
    BoundingBox bbox_hole = get_extents(actual_hole_perimeter);

    Flow hole_flow = Flow::new_from_config_width(frExternalPerimeter, region_config.outer_wall_line_width, print_config.nozzle_diameter.get_at(0), object_config.layer_height);
    // Hole: Original radius 10.0.
    // Target from percentage: 10.0 * 1.01 = 10.1
    // Target after absolute offset: 10.1 + 0.1 = 10.2 (radius)
    // Centerline (outermost perimeter of hole): 10.2 + (hole_flow.width() / 2.0)
    REQUIRE(unscale(bbox_hole.max.x()) == Approx(10.2 + hole_flow.width()/2.0).margin(0.03));
}

// TODO: Add tests for Arachne (might need to check bounding boxes or average positions due to variable width)
// TODO: Add tests for xy_contour_compensation interaction (similar to hole_compensation but for outer contours)
