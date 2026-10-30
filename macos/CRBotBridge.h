#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CRBotBackend CRBotBackend;

// One backend per process. The directory must already exist and be writable.
CRBotBackend* crbot_create(const char* log_directory);
// Scan/connect are asynchronous. Empty endpoint scans USB and local emulators.
void crbot_discover(CRBotBackend* backend, const char* endpoint);
void crbot_cancel_discovery(CRBotBackend* backend);
// 1 = start/retry, 2 = stop, 3 = wait, 4 = disconnect this bot.
void crbot_device_action(CRBotBackend* backend, const char* serial, int action);
void crbot_stop_all(CRBotBackend* backend);
// Returned UTF-8 JSON is owned by the caller; use crbot_free_string.
char* crbot_snapshot(CRBotBackend* backend);
void crbot_free_string(char* value);
void crbot_clear_logs(CRBotBackend* backend);
// Shutdown cancels work immediately; poll until ready before destroying.
void crbot_shutdown(CRBotBackend* backend);
int crbot_shutdown_complete(CRBotBackend* backend);
void crbot_destroy(CRBotBackend* backend);

#ifdef __cplusplus
}
#endif
