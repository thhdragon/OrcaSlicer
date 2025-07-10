#ifndef slic3r_FillHueForge_hpp_
#define slic3r_FillHueForge_hpp_

#include "FillRectilinear.hpp"

namespace Slic3r {

class Surface;
struct FillParams;

class FillHueForge : public FillRectilinear
{
public:
    FillHueForge() = default;
    ~FillHueForge() override = default;

    Fill* clone() const override { return new FillHueForge(*this); }
    
    // This will be the main method to override for custom HueForge logic
    // Polylines fill_surface(const Surface *surface, const FillParams &params) override; // Keep this if direct polyline generation is simpler initially
    void fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out) override;


    // We might need to override this if we change how infill connects significantly
    // static void connect_infill_hueforge(Polylines &&infill_ordered, const ExPolygon &boundary, Polylines &polylines_out, const double spacing, const FillParams &params);

protected:
    // If we need a custom version of _fill_surface_single (from FillRectilinear's parent Fill.hpp, though FillRectilinear itself uses fill_surface_by_lines)
    // void _fill_surface_single(
    //     const FillParams                &params,
    //     unsigned int                     thickness_layers,
    //     const std::pair<float, Point>   &direction,
    //     ExPolygon                        expolygon,
    //     Polylines                       &polylines_out
    // ) override;
};

} // namespace Slic3r

#endif // slic3r_FillHueForge_hpp_
