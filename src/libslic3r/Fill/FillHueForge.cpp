#include "FillHueForge.hpp"
#include "../Surface.hpp"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntityCollection.hpp" // Required for ExtrusionEntitiesPtr and ExtrusionEntityCollection
#include "../Flow.hpp"                     // Required for Flow

namespace Slic3r {

void FillHueForge::fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out)
{
    bool hueforge_active = false;
    const PrintObjectConfig* po_config_ptr = nullptr;

    if (surface && surface->layer() && surface->layer()->object()) {
        po_config_ptr = &surface->layer()->object()->config();
        const ConfigOptionBool* hc_opt = po_config_ptr->option<ConfigOptionBool>("hueforge_mode");
        if (hc_opt) {
            hueforge_active = hc_opt->value;
        }
    }

    if (hueforge_active && po_config_ptr) {
        Polylines all_polylines;

        double nozzle_diameter = params.flow.nozzle_diameter();
        if (nozzle_diameter <= 0) nozzle_diameter = 0.4;
        double small_region_area_threshold = scale_(scale_( (2.0 * nozzle_diameter) * (2.0 * nozzle_diameter) ));

        // Create a mutable copy of PrintRegionConfig if we need to change it.
        PrintRegionConfig modified_region_config;
        if (params.config) {
            modified_region_config = *params.config;
        }
        // Set a small infill_wall_overlap for HueForge mode.
        // This is a percentage of the sparse infill line width.
        // TODO: Make this (5.0) a new ConfigOption: `hueforge_infill_wall_overlap_percent`
        modified_region_config.infill_wall_overlap.value = 5.0;
        FillParams hueforge_params = params;
        hueforge_params.config = &modified_region_config; // Point to our modified region config

        // Adjust anchor lengths for HueForge mode to prefer shorter or no anchors.
        // TODO: Make these configurable: `hueforge_anchor_length`, `hueforge_anchor_length_max`
        hueforge_params.anchor_length = 0;
        hueforge_params.anchor_length_max = scale_(0.25 * nozzle_diameter);


        for (const ExPolygon& region_expoly : surface->expolygon) {
            FillParams current_iter_params = hueforge_params; // Start with HueForge-tuned params
            bool is_small_region = region_expoly.area() < small_region_area_threshold;
            if (is_small_region) {
                // For very small regions, force 100% density to ensure they are "painted".
                current_iter_params.density = 1.0f;
            }

            Surface current_surface_part = *surface;
            current_surface_part.expolygon = region_expoly; // Process this part of the surface
          
            // Call the parent's (FillRectilinear) fill_surface method.
            // This method internally calculates `this->overlap` based on `current_iter_params.config->infill_wall_overlap`
            // and `current_iter_params.flow.width()`.
            // It then calls `fill_surface_by_lines` which uses `this->overlap` and `this->spacing`.
            Polylines region_polylines = FillRectilinear::fill_surface(&current_surface_part, current_iter_params);
            all_polylines.insert(all_polylines.end(), region_polylines.begin(), region_polylines.end());
        }

        return all_polylines;

    } else {
        // If not in HueForge mode, just behave like normal Rectilinear infill.
        // This could be a new config: hueforge_infill_wall_overlap (e.g. 0.05mm or 10%)
        // The base `this->overlap` is set by FillRectilinear::fill_surface_by_lines using:
        // `scale_(this->overlap - (0.5 - INFILL_OVERLAP_OVER_SPACING) * this->spacing))` for aoffset1 (outer)
        // `scale_(this->overlap - 0.5f * this->spacing))` for aoffset2 (inner for connections)
        // For HueForge, we might want `this->overlap` to be small, e.g., equal to `0.5 * this->spacing` for aoffset2
        // so that the inner connecting boundary is right at the perimeter.
        // And for aoffset1, perhaps `0.5 * this->spacing - small_positive_value` to ensure it doesn't go outside.
        // This requires careful modification of how `fill_surface_by_lines` is called or reimplemented.
        // For now, we'll pass the existing params and rely on the parent call.
        // Later, we can introduce `modified_params.overlap = some_hueforge_specific_value;`

        // Iterate over the individual polygons within the surface's ExPolygon
        // (surface->expolygon is what FillRectilinear::fill_surface iterates over internally)
        for (const ExPolygon& region_expoly : surface->expolygon) {
            // TODO: Implement actual small region detection and handling.
            // This is a placeholder. A real implementation would check the width/area of `region_expoly`.
            bool is_small_region = false; // region_expoly.area() < small_region_area_threshold;

            // A more robust check for "small" might be to find the medial axis or estimate thickness.
            // For now, we'll treat all regions under HueForge mode with potentially modified params.

            if (is_small_region) {
                // Specific logic for small regions:
                // - Ensure it's filled, possibly with adjusted density or a centerline approach.
                // - May need to generate polylines directly here.
                // Example: force higher density or a specific pattern for small areas.
                FillParams small_region_params = modified_params;
                // small_region_params.density = std::max(small_region_params.density, 0.5f); // Ensure at least 50% for small areas
                // small_region_params.pattern = ipLine; // Or a simple line fill

                // Create a temporary Surface for this small region to pass to parent
                Surface small_surface = *surface;
                small_surface.expolygon = region_expoly; // Process only this small part
                Polylines region_polylines = FillRectilinear::fill_surface(&small_surface, small_region_params);
                all_polylines.insert(all_polylines.end(), region_polylines.begin(), region_polylines.end());
            } else {
                // Logic for larger regions (or default HueForge behavior)
                Surface current_surface_part = *surface;
                current_surface_part.expolygon = region_expoly;
                Polylines region_polylines = FillRectilinear::fill_surface(&current_surface_part, modified_params);
                all_polylines.insert(all_polylines.end(), region_polylines.begin(), region_polylines.end());
            }
        }

        // TODO: Potentially apply custom connection logic for HueForge if needed
        // For now, rely on parent's connection logic (or lack thereof if dont_connect is true)

        return all_polylines;

    } else {
        // If not in HueForge mode, just behave like normal Rectilinear infill.
        return FillRectilinear::fill_surface(surface, params);
    }
}

// Implement static void FillHueForge::connect_infill_hueforge if needed

} // namespace Slic3r
