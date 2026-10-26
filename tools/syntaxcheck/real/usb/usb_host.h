/* usb_host.h CURADO para o syntaxcheck do p4da-v2 (tools/syntaxcheck/real/).
 *
 * Nomes de enums/structs/protótipos COPIADOS do header real do IDF v5.5.1
 * (components/usb/include/usb/usb_host.h, conferidos linha a linha em
 * 2026-10-25): usb_host_client_event_t (NEW_DEV/DEV_GONE),
 * usb_host_client_event_msg_t (union anônima new_dev.address /
 * dev_gone.dev_hdl), usb_host_client_event_cb_t, usb_host_client_config_t
 * (max_num_event_msg + union async), usb_host_config_t, handles e as
 * funções de lib/client/device usadas pelo pda_usb/pda_audio.
 * Sem os includes transitivos do IDF (esp_assert/sdkconfig/...): o alvo
 * é checar NOSSO código contra a API real (drift guard do watcher
 * v2.2.2). Doc-comments omitidos. Ao subir de IDF: re-conferir nomes.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct usb_host_client_handle_s *usb_host_client_handle_t;
typedef struct usb_device_handle_s *usb_device_handle_t;

#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS 0x01
#define USB_HOST_LIB_EVENT_FLAGS_ALL_FREE   0x02

typedef enum {
    USB_HOST_CLIENT_EVENT_NEW_DEV,
    USB_HOST_CLIENT_EVENT_DEV_GONE,
} usb_host_client_event_t;

typedef struct {
    usb_host_client_event_t event;
    union {
        struct {
            uint8_t address;
        } new_dev;
        struct {
            usb_device_handle_t dev_hdl;
        } dev_gone;
    };
} usb_host_client_event_msg_t;

typedef void (*usb_host_client_event_cb_t)(const usb_host_client_event_msg_t *event_msg,
                                           void *arg);

typedef bool (*usb_host_enum_filter_cb_t)(const void *dev_desc);  /* adaptação: real vem de usb_types_stack.h */

typedef struct {
    bool skip_phy_setup;
    bool root_port_unpowered;
    int intr_flags;
    usb_host_enum_filter_cb_t enum_filter_cb;
    struct {
        uint32_t nptx_fifo_lines;
        uint32_t ptx_fifo_lines;
        uint32_t rx_fifo_lines;
    } fifo_settings_custom;
    unsigned peripheral_map;
} usb_host_config_t;

typedef struct {
    bool is_synchronous;
    int max_num_event_msg;
    union {
        struct {
            usb_host_client_event_cb_t client_event_callback;
            void *callback_arg;
        } async;
    };
} usb_host_client_config_t;

/* ---- library ---- */
esp_err_t usb_host_install(const usb_host_config_t *config);
esp_err_t usb_host_uninstall(void);
esp_err_t usb_host_lib_handle_events(TickType_t timeout_ticks, uint32_t *event_flags_ret);
esp_err_t usb_host_device_free_all(void);

/* ---- client ---- */
esp_err_t usb_host_client_register(const usb_host_client_config_t *client_config,
                                   usb_host_client_handle_t *client_hdl_ret);
esp_err_t usb_host_client_deregister(usb_host_client_handle_t client_hdl);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client_hdl,
                                        TickType_t timeout_ticks);

/* ---- device ---- */
esp_err_t usb_host_device_open(usb_host_client_handle_t client_hdl, uint8_t dev_addr,
                               usb_device_handle_t *dev_hdl_ret);
esp_err_t usb_host_device_close(usb_host_client_handle_t client_hdl,
                                usb_device_handle_t dev_hdl);

#ifdef __cplusplus
}
#endif
