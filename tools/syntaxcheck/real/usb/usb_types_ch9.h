/* usb_types_ch9.h CURADO (mínimo) p/ syntaxcheck — ver nota em usb_host.h.
 * Só os tipos que uac.h/uac_host.h referenciam; opacos porque nosso código
 * não toca seus campos. Nomes conferidos no IDF v5.5.1
 * (components/usb/include/usb/usb_types_ch9.h). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usb_standard_desc_s usb_standard_desc_t;
typedef struct usb_config_desc_s usb_config_desc_t;

typedef enum {
    USB_TERMINAL_TYPE_UNDEFINED = 0x0100,
    USB_TERMINAL_TYPE_STREAMING = 0x0101,
} usb_terminal_type_t;

#ifdef __cplusplus
}
#endif
