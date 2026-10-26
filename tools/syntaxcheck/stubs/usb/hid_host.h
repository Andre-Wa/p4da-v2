/* stub de usb/hid_host.h p/ o estágio de componentes do syntaxcheck.
 * O header REAL não é público em repo browsável; estas assinaturas são
 * DERIVADAS DO USO em usb_hid_keyboard.cpp (código v1 com build de alvo
 * provado desde M2). Serve p/ checar o código NOVO (watcher) contra o
 * usb_host.h REAL; drift do hid em si segue coberto pelo build de alvo. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hid_host_device_s *hid_host_device_handle_t;

typedef enum {
    HID_HOST_INTERFACE_EVENT_INPUT_REPORT = 0,
    HID_HOST_INTERFACE_EVENT_DISCONNECTED,
} hid_host_interface_event_t;

typedef enum {
    HID_HOST_DRIVER_EVENT_CONNECTED = 0,
} hid_host_driver_event_t;

#define HID_SUBCLASS_BOOT_INTERFACE 1
#define HID_PROTOCOL_KEYBOARD       1
#define HID_REPORT_PROTOCOL_BOOT    0

typedef struct { uint8_t sub_class; uint8_t proto; } hid_host_dev_params_t;

typedef void (*hid_host_interface_callback_t)(hid_host_device_handle_t,
                                              hid_host_interface_event_t, void *);
typedef struct {
    hid_host_interface_callback_t callback;
    void *callback_arg;
} hid_host_device_config_t;

typedef void (*hid_host_driver_callback_t)(hid_host_device_handle_t,
                                           hid_host_driver_event_t, void *);
typedef struct {
    bool create_background_task;
    int task_priority;
    int stack_size;
    int core_id;
    hid_host_driver_callback_t callback;
    void *callback_arg;
} hid_host_driver_config_t;

esp_err_t hid_host_install(const hid_host_driver_config_t *config);
esp_err_t hid_host_uninstall(void);
esp_err_t hid_host_device_open(hid_host_device_handle_t h,
                               const hid_host_device_config_t *config);
esp_err_t hid_host_device_start(hid_host_device_handle_t h);
esp_err_t hid_host_device_close(hid_host_device_handle_t h);
esp_err_t hid_host_device_get_params(hid_host_device_handle_t h,
                                     hid_host_dev_params_t *params);
esp_err_t hid_host_device_get_raw_input_report_data(hid_host_device_handle_t h,
                                                    uint8_t *data, size_t size,
                                                    size_t *out_len);
esp_err_t hid_class_request_set_protocol(hid_host_device_handle_t h, int proto);
esp_err_t hid_class_request_set_idle(hid_host_device_handle_t h, int dur, int report_id);

#ifdef __cplusplus
}
#endif
