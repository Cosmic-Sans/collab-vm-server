#pragma once

#include <guacenc/instructions.h>

namespace CollabVm::Server
{
struct GuacamoleScreenshot
{
  std::unique_ptr<guacenc_display,
                  decltype(&guacenc_display_free)>
    display_ = {guacenc_display_alloc(nullptr, nullptr, 0, 0, 0), &guacenc_display_free};

  void WriteInstruction(Guacamole::GuacServerInstruction::Reader instruction)
  {
    guacenc_handle_instruction(display_.get(), instruction);
  }

  template<typename TWriteCallback>
  bool CreateScreenshot(std::uint32_t max_width, std::uint32_t max_height,
      TWriteCallback&& callback)
  {
    // The default layer should now contain the flattened image
    const auto default_layer = guacenc_display_get_layer(display_.get(), 0);
    if (!default_layer->buffer)
    {
      return false;
    }
    const auto& surface = *default_layer->buffer;
    if (!surface.cairo || !surface.surface)
    {
      return false;
    }

    int width;
    int height;
    float scale_xy;
    if (max_width == 0 || max_height == 0)
    {
      width = surface.width;
      height = surface.height;
      scale_xy = 1;
    }
    else if (surface.width > surface.height)
    {
      width = max_width;
      scale_xy = float(width) / surface.width;
      height = scale_xy * surface.height;
    }
    else
    {
      height = max_height;
      scale_xy = float(height) / surface.height;
      width = scale_xy * surface.width;
    }

    const auto target =
      cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    const auto cairo_context = cairo_create(target);

    if (scale_xy != 1)
    {
      cairo_scale(cairo_context, scale_xy, scale_xy);
    }

    cairo_set_source_surface(cairo_context, surface.surface, 0, 0);
    cairo_paint(cairo_context);

    const auto result =
      cairo_surface_write_to_png_stream(target,
        [](void* closure,
           const unsigned char* data,
           unsigned int length)
        {
          const auto& callback = *reinterpret_cast<TWriteCallback*>(closure);
          callback(gsl::span(reinterpret_cast<const std::byte*>(data), length));
          return CAIRO_STATUS_SUCCESS;
        }, &callback);

    cairo_destroy(cairo_context);
    cairo_surface_destroy(target);

    return result == CAIRO_STATUS_SUCCESS;
  }
};
} // namespace CollabVm::Server
