#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"


// Definición de credenciales WiFi y parámetros
#define WIFI_SSID      "S22Raul"
#define WIFI_PASS      "01octubre2004"
#define MAX_LOGS       10

static const char *TAG = "WEBMON";

// Estructura para almacenar un log
typedef struct {
    char mensaje[128]; // Mensaje del log
    char tipo[16];  // Tipo: info, warning, error
    uint32_t timestamp;  // Marca de tiempo
} LogEntry;

// Estructura para almacenar las métricas del sistema
typedef struct {
    int rssi; // Intensidad de señal WiFi
    float cpu_usage; // Uso de CPU (simulado)
    char tareas[512];  // Lista de tareas activas
    LogEntry logs[MAX_LOGS]; // Array de logs
    int log_count; // Número de logs actuales
} SystemMetrics;

static SystemMetrics metrics; // Instancia global de métricas
static SemaphoreHandle_t metrics_mutex; // Mutex para proteger acceso a métricas

// HTML que se sirve como interfaz web
static const char *HTML_CONTENT =
"<!DOCTYPE html>"
"<html><head><meta charset='utf-8'><title>Monitor ESP32</title>"
"<style>"
"body{font-family:sans-serif;background:#222;color:#fff;margin:0;padding:0;}"
".container{max-width:700px;margin:40px auto;background:#333;padding:24px;border-radius:10px;box-shadow:0 0 16px #0007;}"
"h1{color:#4ecdc4;}"
".metric{margin-bottom:14px;font-size:1.2em;}"
".log-info{color:#4ecdc4;}.log-warning{color:#ffe66d;}.log-error{color:#ff6b6b;}"
".log{margin-bottom:4px;}"
"pre{background:#222;padding:8px;border-radius:5px;}"
"</style></head><body>"
"<div class='container'>"
"<h1>Monitor ESP32</h1>"
"<div class='metric'><b>Intensidad de señal WiFi:</b> <span id='rssi'>-</span> dBm</div>"
"<div class='metric'><b>Uso de CPU:</b> <span id='cpu'>-</span> %</div>"
"<div class='metric'><b>Tareas activas:</b></div><pre id='tasks'>Cargando...</pre>"
"<div class='metric'><b>Logs:</b></div><div id='logs'></div>"
"</div>"
"<script>"
"function updateMetrics(){"
"fetch('/metrics').then(r=>r.json()).then(data=>{"
"document.getElementById('rssi').textContent=data.rssi;"
"document.getElementById('cpu').textContent=data.cpu_usage;"
"document.getElementById('tasks').textContent=data.tareas;"
"let logsDiv=document.getElementById('logs');logsDiv.innerHTML='';"
"data.logs.forEach(function(log){"
"let span=document.createElement('span');"
"span.className='log log-'+log.tipo;"
"span.textContent='['+log.timestamp+'] '+log.tipo.toUpperCase()+': '+log.mensaje;"
"logsDiv.appendChild(span);logsDiv.appendChild(document.createElement('br'));"
"});"
"});"
"}"
"setInterval(updateMetrics,2000);"
"updateMetrics();"
"</script></body></html>";

// Handler para servir la página principal (HTML)
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_CONTENT, strlen(HTML_CONTENT));
    return ESP_OK;
}

// Handler para servir las métricas como JSON
static esp_err_t metrics_get_handler(httpd_req_t *req) {
    // Verifica que el mutex esté inicializado
    if (metrics_mutex == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    // Toma el mutex para acceder a las métricas de forma segura
    if (xSemaphoreTakeRecursive(metrics_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Crea un objeto JSON y agrega las métricas
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        xSemaphoreGiveRecursive(metrics_mutex);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Agrega los logs al JSON
    cJSON_AddNumberToObject(json, "rssi", metrics.rssi);
    cJSON_AddNumberToObject(json, "cpu_usage", metrics.cpu_usage);
    cJSON_AddStringToObject(json, "tareas", metrics.tareas);

    cJSON *logs = cJSON_CreateArray();
    if (logs) {
        for (int i = 0; i < metrics.log_count; ++i) {
            cJSON *log = cJSON_CreateObject();
            if (log) {
                cJSON_AddStringToObject(log, "mensaje", metrics.logs[i].mensaje);
                cJSON_AddStringToObject(log, "tipo", metrics.logs[i].tipo);
                cJSON_AddNumberToObject(log, "timestamp", metrics.logs[i].timestamp);
                cJSON_AddItemToArray(logs, log);
            }
        }
        cJSON_AddItemToObject(json, "logs", logs);
    }

    // Serializa el JSON y lo envía como respuesta
    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str == NULL) {
        cJSON_Delete(json);
        xSemaphoreGiveRecursive(metrics_mutex);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    // Libera recursos
    free(json_str);
    cJSON_Delete(json);
    xSemaphoreGiveRecursive(metrics_mutex);
    return ESP_OK;
}

// Inicializa y arranca el servidor web
static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    httpd_handle_t server = NULL;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);


	// Registra el handler "/metrics"
        httpd_uri_t metrics_uri = {
            .uri = "/metrics",
            .method = HTTP_GET,
            .handler = metrics_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &metrics_uri);
    }
    return server;
}

// Tarea periódica para actualizar las métricas del sistema
void task_metrics(void *pvParam) {
    char task_buf[512];
    while (1) {
        if (xSemaphoreTakeRecursive(metrics_mutex, portMAX_DELAY) == pdTRUE) {
	    // Obtiene intensidad de señal WiFi
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                metrics.rssi = ap_info.rssi;
            }
	    // Simula el uso de CPU (aleatorio)
            metrics.cpu_usage = (float)(rand() % 100);

	    // Obtiene lista de tareas activas de FreeRTOS
            vTaskList(task_buf);
            strncpy(metrics.tareas, task_buf, sizeof(metrics.tareas)-1);
            metrics.tareas[sizeof(metrics.tareas)-1] = 0;

            xSemaphoreGiveRecursive(metrics_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // Espera 2 segundos
    }
}

// Tarea periódica para agregar logs simulados
void task_logs(void *pvParam) {
    int tick = 0;
    while (1) {
        if (xSemaphoreTakeRecursive(metrics_mutex, portMAX_DELAY) == pdTRUE) {
	    // Si el array de logs está lleno, elimina el más antiguo
            if (metrics.log_count == MAX_LOGS) {
                for (int i = 1; i < MAX_LOGS; ++i)
                    metrics.logs[i-1] = metrics.logs[i];
                metrics.log_count--;
            }
	    // Agrega un nuevo log
            LogEntry *log = &metrics.logs[metrics.log_count++];
            snprintf(log->mensaje, sizeof(log->mensaje), "Evento %d registrado", tick);
            strcpy(log->tipo, (tick%3==0)?"info":((tick%3==1)?"warning":"error"));
            log->timestamp = (uint32_t)xTaskGetTickCount()/1000;
            xSemaphoreGiveRecursive(metrics_mutex);
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// Handler de eventos WiFi, reconecta si se pierde la conexión
static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                             int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi desconectado, intentando reconectar...");
    }
}

// Inicializa la conexión WiFi en modo estación
static void wifi_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                      &wifi_event_handler, NULL, &instance_any_id);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = 5
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_start();
    esp_wifi_connect();
}

// Verifica periódicamente si el WiFi está conectado, si no, reconecta
void check_wifi_connection() {
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info) != ESP_OK || 
        ip_info.ip.addr == 0) {
        ESP_LOGE(TAG, "WiFi desconectado, reconectando...");
        esp_wifi_disconnect();
        esp_wifi_connect();
    }
}

// Función principal de la aplicación
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init()); // Inicializa almacenamiento no volátil

    // Crea el mutex para proteger las métricas
    metrics_mutex = xSemaphoreCreateRecursiveMutex();
    if (metrics_mutex == NULL) {
        ESP_LOGE(TAG, "Error al crear el mutex");
        return;
    }
    memset(&metrics, 0, sizeof(metrics)); // Inicializa las métricas a cero

    srand((unsigned)time(NULL)); // Inicializa la semilla aleatoria
    wifi_init(); // Inicializa el WiFi

    // Espera hasta que se obtenga una IP válida
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;

    ESP_LOGI(TAG, "Conectando a WiFi...");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "¡Conectado! Dirección IP: " IPSTR, IP2STR(&ip_info.ip));
            break;
        }
    }

    start_webserver(); // Inicia el servidor web

    // Crea las tareas para métricas y logs
    xTaskCreate(task_metrics, "task_metrics", 4096, NULL, 5, NULL);
    xTaskCreate(task_logs, "task_logs", 4096, NULL, 5, NULL);

    // Bucle principal: verifica conexión WiFi periódicamente
    while (1) {
        check_wifi_connection();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}