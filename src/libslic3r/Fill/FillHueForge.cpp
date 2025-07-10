#include "FillHueForge.hpp"
#include "../Surface.hpp"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntityCollection.hpp" // Required for ExtrusionEntitiesPtr and ExtrusionEntityCollection
#include "../Flow.hpp"                     // Required for Flow

namespace Slic3r {

// This method is called by Fill::fill_surface_extrusion
Polylines FillHueForge::fill_surface(const Surface *surface, const FillParams &params)
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
        if (nozzle_diameter <= 0) nozzle_diameter = 0.4; // Default if not set
        double small_region_area_threshold = scale_(scale_((2.0 * nozzle_diameter) * (2.0 * nozzle_diameter)));

        PrintRegionConfig modified_region_config;
        if (params.config) {
            modified_region_config = *params.config;
        }

        // Use new config options for HueForge parameters
        if (po_config_ptr->option<ConfigOptionPercent>("hueforge_infill_wall_overlap")) {
            modified_region_config.infill_wall_overlap.value = po_config_ptr->opt_float("hueforge_infill_wall_overlap");
        } else {
            modified_region_config.infill_wall_overlap.value = 5.0; // Fallback if not found
        }

        FillParams hueforge_params = params;
        hueforge_params.config = &modified_region_config;

        if (po_config_ptr->option<ConfigOptionFloatOrPercent>("hueforge_anchor_length")) {
            hueforge_params.anchor_length = scale_(po_config_ptr->get_abs_value("hueforge_anchor_length", params.flow.width()));
        } else {
            hueforge_params.anchor_length = 0; // Fallback
        }

        if (po_config_ptr->option<ConfigOptionFloatOrPercent>("hueforge_anchor_length_max")) {
            hueforge_params.anchor_length_max = scale_(po_config_ptr->get_abs_value("hueforge_anchor_length_max", nozzle_diameter));
        } else {
            hueforge_params.anchor_length_max = scale_(0.25 * nozzle_diameter); // Fallback
        }

        // The parent FillRectilinear::fill_surface iterates over surface->expolygon.
        // We need to apply our modified params for that call.
        // We also need to handle small regions by adjusting density *before* calling the parent.
        // This is tricky because the parent iterates internally.
        // A cleaner way might be to replicate the iteration here.

        for (const ExPolygon& region_expoly : surface->expolygon) {
            FillParams current_iter_params = hueforge_params; // Start with HueForge-tuned params for this specific region
            bool is_small_region = region_expoly.area() < small_region_area_threshold;

            if (is_small_region) {
                current_iter_params.density = 1.0f;
            }

            // Create a temporary surface for this specific region_expoly
            Surface current_surface_part = *surface;
            current_surface_part.expolygon = region_expoly; // Process this part of the surface
          
            // Call the parent's (FillRectilinear) fill_surface method.
            Polylines region_polylines = FillRectilinear::fill_surface(&current_surface_part, current_iter_params);
            all_polylines.insert(all_polylines.end(), region_polylines.begin(), region_polylines.end());
        }
        return all_polylines;

    } else {
        // If not in HueForge mode, or config is missing, behave like normal Rectilinear infill.
        return FillRectilinear::fill_surface(surface, params);
    }
}

// Implement static void FillHueForge::connect_infill_hueforge if needed

} // namespace Slic3r
