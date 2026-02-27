#include "apkm.h"
#include <prom.h>
#include <promhttp.h>

// Métriques Prometheus
static prom_gauge_t* packages_total;
static prom_gauge_t* vulnerabilities_total;
static prom_counter_t* installs_total;
static prom_histogram_t* operation_duration;

void init_metrics(void) {
    prom_collector_registry_default_init();
    
    packages_total = prom_gauge_new("apkm_packages_total",
                                    "Total number of installed packages",
                                    0, NULL);
    
    vulnerabilities_total = prom_gauge_new("apkm_vulnerabilities_total",
                                          "Total number of vulnerabilities found",
                                          0, NULL);
    
    installs_total = prom_counter_new("apkm_installs_total",
                                      "Total number of package installations",
                                      0, NULL);
    
    operation_duration = prom_histogram_new("apkm_operation_duration_seconds",
                                           "Duration of operations",
                                           prom_histogram_buckets_linear(0.1, 0.5, 10),
                                           0, NULL);
    
    prom_collector_registry_must_register(packages_total);
    prom_collector_registry_must_register(vulnerabilities_total);
    prom_collector_registry_must_register(installs_total);
    prom_collector_registry_must_register(operation_duration);
}

// Serveur HTTP pour métriques
void* metrics_server(void* arg) {
    struct MHD_Daemon* daemon = promhttp_start_daemon(
        MHD_USE_SELECT_INTERNALLY | MHD_USE_DEBUG,
        9090, NULL, NULL);
    
    if (!daemon) return NULL;
    
    while (1) {
        sleep(1);
        update_metrics();
    }
    
    MHD_stop_daemon(daemon);
    return NULL;
}

// Notification en temps réel via WebSocket
void notify_clients(const char* event, const char* data) {
    // WebSocket broadcast
    lws_broadcast(event, data);
    
    // Systemd journal
    sd_journal_send("MESSAGE=%s: %s", event, data,
                    "PRIORITY=%d", LOG_INFO,
                    "APKM_EVENT=%s", event,
                    NULL);
    
    // D-Bus signal
    dbus_connection_send_signal(event, data);
}
