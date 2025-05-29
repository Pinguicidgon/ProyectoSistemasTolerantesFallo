# Memoria - Servidor WEB

## Proyecto realizado por:
- Raúl Cid González  
- Pedro Zuñeda Diego  
- Emilio José Román Rosales  

---

## Tabla de contenido
1. [Resumen](#resumen)
2. [Cumplimiento del enunciado](#cumplimiento-del-enunciado)
3. [Funcionalidad general](#funcionalidad-general)
4. [Información que se muestra en la web](#información-que-se-muestra-en-la-web)
5. [Arquitectura del programa](#arquitectura-del-programa)
6. [Proceso de uso](#proceso-de-uso)
7. [Vista de usuario](#vista-de-usuario)

---

## Resumen

Este proyecto consiste en el desarrollo de un monitor web embebido para el microcontrolador **ESP32-S3**, utilizando el sistema operativo **FreeRTOS**. El objetivo es mostrar métricas del sistema en tiempo real a través de una página web accesible desde cualquier navegador en la misma red WiFi.

---

## Cumplimiento del enunciado

Este proyecto cumple completamente con los objetivos definidos en el enunciado. La placa **ESP32-S3** actúa como un servidor web autónomo que:

- Mide la intensidad de la señal WiFi (RSSI) en tiempo real, utilizando la API `esp_wifi_sta_get_ap_info`.
- Muestra el uso de CPU, simulado para ilustrar el comportamiento del sistema.
- Muestra el estado de las tareas activas, mediante la función `vTaskList` de FreeRTOS.
- Muestra un log de eventos del sistema, que se genera periódicamente con niveles tipo `info`, `warning` y `error`.
- Todo esto es accesible desde una interfaz web moderna, servida directamente desde la propia **ESP32-S3** a través de un servidor HTTP embebido.

---

## Funcionalidad general

Al arrancar, el **ESP32-S3** se conecta automáticamente a una red WiFi (con SSID y contraseña definidos en el código). Una vez conectado, inicia un servidor web HTTP que responde en dos rutas principales:

- `/` → Entrega una página HTML moderna con estilos CSS y JavaScript integrado.  
- `/metrics` → Devuelve un objeto JSON con los datos actualizados del sistema.

---

## Información que se muestra en la web

- **Intensidad de señal WiFi (RSSI):** obtenida mediante la API de `esp_wifi`.
- **Uso de CPU simulado:** generado aleatoriamente para efectos de visualización.
- **Tareas activas:** obtenidas con `vTaskList`, útil para monitorizar el estado del sistema.
- **Logs del sistema:** registros generados por el ESP32-S3 cada 3 segundos, con distintos niveles (`info`, `warning`, `error`) y su correspondiente marca de tiempo.

> La página web se actualiza automáticamente cada 2 segundos gracias a JavaScript, sin necesidad de recargar manualmente.

---

## Arquitectura del programa

- Se definen estructuras (`SystemMetrics` y `LogEntry`) para almacenar las métricas y logs.
- Dos tareas de FreeRTOS gestionan los datos:
  - `task_metrics`: actualiza el RSSI, el uso de CPU y la lista de tareas.
  - `task_logs`: añade logs periódicos al registro circular (máximo 10 logs).
- Se usa un mutex recursivo (`xSemaphoreCreateRecursiveMutex`) para evitar conflictos al acceder a los datos compartidos desde distintas tareas.

---

## Proceso de uso

1. Se configuran las credenciales WiFi en el código.
2. Se compila y flashea el proyecto en el ESP32-S3 mediante `idf.py`.
3. Al conectarse, el ESP32-S3 muestra su dirección IP por consola.
4. Desde el navegador, se accede a esa IP y se puede ver el monitor funcionando en tiempo real.

---
# Vista de usuario

![Captura de pantalla 2025-05-29 130034](https://github.com/user-attachments/assets/0e9d50be-b6f7-4d13-aab6-5b100c046c68)


---
