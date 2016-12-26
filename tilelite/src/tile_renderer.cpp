#include "tile_renderer.h"
#include <mapnik/agg_renderer.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/image.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/map.hpp>
#include <mapnik/well_known_srs.hpp>
#include <memory>
#include <mutex>
#include "tl_log.h"
#include "tl_math.h"

struct tile_renderer {
  tile_renderer(std::unique_ptr<mapnik::Map> map) : map(std::move(map)) {}
  std::unique_ptr<mapnik::Map> map;
};

void mapnik_global_init(const char* plugins_path, const char* fonts_path) {
  static std::once_flag done;

  std::call_once(done, [=]() {
    mapnik::logger::instance().set_severity(mapnik::logger::none);
    mapnik::datasource_cache::instance().register_datasources(plugins_path);
    mapnik::freetype_engine::register_fonts(fonts_path, true);
  });
}

tile_renderer* tile_renderer_create(const char* mapnik_xml_path,
                                    const char* plugins_path,
                                    const char* fonts_path) {
  try {
    mapnik_global_init(plugins_path, fonts_path);
    std::unique_ptr<mapnik::Map> map =
        std::unique_ptr<mapnik::Map>(new mapnik::Map());
    mapnik::load_map(*map, mapnik_xml_path);
    return new tile_renderer(std::move(map));
  } catch (std::exception& e) {
    tl_log("mapnik load error: %s", e.what());
  }

  return nullptr;
}

void tile_renderer_destroy(tile_renderer* renderer) { delete renderer; }

int32_t render_tile(tile_renderer* renderer, const tl_tile* tile,
                    image* image) {
  vec3d p1_xyz{double(tile->x), double(tile->y), double(tile->z)};
  vec3d p2_xyz{double(tile->x + 1), double(tile->y + 1), double(tile->z)};
  vec2d p1 = xyz_to_latlon(p1_xyz);
  vec2d p2 = xyz_to_latlon(p2_xyz);

  mapnik::lonlat2merc(&p1.x, &p1.y, 1);
  mapnik::lonlat2merc(&p2.x, &p2.y, 1);

  mapnik::box2d<double> bbox(p1.x, p1.y, p2.x, p2.y);

  renderer->map->resize(tile->w, tile->h);
  renderer->map->zoom_to_box(bbox);
  if (renderer->map->buffer_size() == 0) {
    renderer->map->set_buffer_size(96);
  }

  assert(tile->w > 0 && tile->h > 0);
  mapnik::image_rgba8 buf(tile->w, tile->h);
  mapnik::agg_renderer<mapnik::image_rgba8> ren(*renderer->map, buf);

  try {
    ren.apply();
  } catch (std::exception& e) {
    tl_log("error rendering tile:\n %s", e.what());
    return 1;
  }

  std::string output_png = mapnik::save_to_string(buf, "png8");

  image->width = tile->w;
  image->height = tile->h;

  image->len = int(output_png.size());
  image->data = (uint8_t*)calloc(1, output_png.size());
  memcpy(image->data, output_png.data(), output_png.size());

  return 0;
}
