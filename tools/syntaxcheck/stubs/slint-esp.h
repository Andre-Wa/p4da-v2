/* stub de slint-esp.h p/ syntaxcheck da camada app no HOST.
 *
 * O real vive no componente gerenciado slint/slint (backend ESP: integra
 * panel esp_lcd + touch + software renderer). Aqui só a superfície que o
 * app_main usa: SlintPlatformConfiguration (campos idênticos aos do
 * slint-esp 1.12.1 que o firmware preenche) + slint_esp_init no-op.
 * Os tipos de pixel/rotação vêm do slint-platform.h REAL do Slint-cpp
 * Linux — os mesmos nomes/enum do alvo. */
#pragma once

#include <slint-platform.h>

#include <vector>

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

template <typename PixelT>
struct SlintPlatformConfiguration {
    slint::PhysicalSize size { 0, 0 };
    esp_lcd_panel_handle_t panel_handle = nullptr;
    esp_lcd_touch_handle_t touch_handle = nullptr;
    std::vector<PixelT> buffer1;
    std::vector<PixelT> buffer2;
    typename slint::platform::SoftwareRenderer::RenderingRotation rotation =
        slint::platform::SoftwareRenderer::RenderingRotation::NoRotation;
    bool byte_swap = false;
};

template <typename PixelT>
inline void slint_esp_init(SlintPlatformConfiguration<PixelT> config)
{
    (void)config;
}
