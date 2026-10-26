#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * usb_host_state.h — a USB Host Library já foi instalada?
 *
 * O install único da Host Library mora em usb_hid_keyboard.cpp
 * (usb_host_lib_task). audio_uac.c é o 2º client e, se chamar
 * uac_host_install() ANTES do host existir, o componente falha e loga
 * "E uac-host: Unable to register USB Host client" (inofensivo: o retry
 * de 200 ms resolvia — mas era uma linha E fixa em todo boot, log de
 * 2026-10-11). Esperar por este flag torna o 1º attempt determinístico.
 * vira false de novo no teardown do light sleep (prepare_sleep) e true
 * no reinstall do wake.
 */
bool usb_host_state_ready(void);

/**
 * v2.2.1: watcher de desconexão em nível de USB Host.
 *
 * Por que existe: os drivers de classe (HID, UAC) só entregam evento de
 * disconnect para device ABERTO (callback por device). Um speaker UAC
 * conectado mas nunca usado (rota no falante) e depois removido deixava
 * o audio_uac com s_present stale=true — e o próximo play chamava
 * uac_host_device_open() num device que não existe: o driver 1.5.0
 * derefere uac_iface NULL dentro do próprio open (uac_host.c:2171,
 * Load access fault no campo de 2026-10-25). O client watcher abaixo
 * segura o handle de todo device enumerado (DEV_GONE do IDF 5.5 só avisa
 * client com device ABERTO) e fecha+avisa no unplug — antes de qualquer
 * open stale. v2.2.2: API real do IDF 5.5 (NEW_DEV/DEV_GONE, ccfg.async).
 */
typedef void (*usb_host_dev_gone_cb)(uint8_t dev_addr);
void usb_host_set_dev_gone_cb(usb_host_dev_gone_cb cb);

/** Teardown do light sleep: solta os handles segurados pelo watcher
 *  (usb_host_device_free_all espera todo close antes do uninstall). */
void usb_host_watcher_close_all(void);

#ifdef __cplusplus
}
#endif
