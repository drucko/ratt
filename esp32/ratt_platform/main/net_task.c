#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/heap_regions.h"
#include "esp_heap_alloc_caps.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "https.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "mbedtls/base64.h"

static const char *TAG = "net_task";


// The examples use simple configurations that you can set via 'make menuconfig'.
#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD
#define WEB_SERVER CONFIG_WEB_SERVER
#define WEB_PORT CONFIG_WEB_PORT
#define WEB_URL_PATH CONFIG_WEB_URL_PATH
#define WEB_BASIC_AUTH_USER CONFIG_WEB_BASIC_AUTH_USER
#define WEB_BASIC_AUTH_PASS CONFIG_WEB_BASIC_AUTH_PASS


#define ACL_BUF_SIZE (80 * 1024)

// FreeRTOS event group to signal when we are connected & ready to make a request
static EventGroupHandle_t wifi_event_group;

// The event group allows multiple bits for each event,
// but we only care about one event - are we connected
// to the AP with an IP?
const int CONNECTED_BIT = BIT0;

#ifdef MBEDTLS_DEBUG_C
#define MBEDTLS_DEBUG_LEVEL 4

/* mbedtls debug function that translates mbedTLS debug output
   to ESP_LOGx debug output.

   MBEDTLS_DEBUG_LEVEL 4 means all mbedTLS debug output gets sent here,
   and then filtered to the ESP logging mechanism.
*/
static void mbedtls_debug(void *ctx, int level, const char *file, int line, const char *str)
{
    const char *MBTAG = "mbedtls";
    char *file_sep;

    /* Shorten 'file' from the whole file path to just the filename
       This is a bit wasteful because the macros are compiled in with
       the full _FILE_ path in each case.
    */
    file_sep = rindex(file, '/');
    if(file_sep)
        file = file_sep+1;

    switch(level) {
    case 1:
        ESP_LOGI(MBTAG, "%s:%d %s", file, line, str);
        break;
    case 2:
    case 3:
        ESP_LOGD(MBTAG, "%s:%d %s", file, line, str);
    case 4:
        ESP_LOGV(MBTAG, "%s:%d %s", file, line, str);
        break;
    default:
        ESP_LOGE(MBTAG, "Unexpected log level %d: %s", level, str);
        break;
    }
}

#endif




static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}


void net_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .password = EXAMPLE_WIFI_PASS,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}


void net_task(void *pvParameters)
{
    char web_url[128];
    int ret;
    char *acl_buf;
    
    snprintf(web_url, sizeof(web_url), "https://%s/%s", WEB_SERVER, WEB_URL_PATH);

    while(1) {
        // Wait for the callback to set the CONNECTED_BIT in the event group.
        xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,false, true, portMAX_DELAY);
        ESP_LOGI(TAG, "Connected to AP...");

        ESP_LOGI(TAG, "Free heap size before malloc = %zu", xPortGetFreeHeapSizeCaps(MALLOC_CAP_8BIT));
        
        acl_buf = pvPortMallocCaps(ACL_BUF_SIZE, MALLOC_CAP_8BIT);

        ESP_LOGI(TAG, "Free heap size after malloc = %zu", xPortGetFreeHeapSizeCaps(MALLOC_CAP_8BIT));
        
        if (acl_buf) {
            ESP_LOGI(TAG, "ACL buffer size = %zu", ACL_BUF_SIZE);
            
            http_init(0);
            ret = http_get(0, web_url, WEB_BASIC_AUTH_USER, WEB_BASIC_AUTH_PASS, acl_buf, ACL_BUF_SIZE);

            // Use POSIX and C standard library functions to work with files.
            ESP_LOGI(TAG, "Opening file on SD card for write...");
            FILE* f = fopen("/sdcard/acl.txt", "w");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to open file for writing...");
                return;
            }
            ret = fwrite(acl_buf, strlen(acl_buf), 1, f);
            if (ret != 1) {
                ESP_LOGE(TAG, "Failed to write to SD card... %d", ret);
            }
            fclose(f);
            ESP_LOGI(TAG, "ACL file written to SD card...");

            // Open file for reading
            ESP_LOGI(TAG, "Reading ACL file from SD card...");
            f = fopen("/sdcard/acl.txt", "r");
            if (f == NULL) {
                ESP_LOGE(TAG, "Failed to open file for reading");
                return;
            }

            char line[128];
            while (fgets(line, sizeof(line), f) == line) {
                ESP_LOGI(TAG, "%s", line);
            }
            fclose(f);

            // All done, unmount partition and disable SDMMC host peripheral
            /*
              esp_vfs_fat_sdmmc_unmount();
              ESP_LOGI(TAG, "Card unmounted");
            */
            
//            ESP_LOGI(TAG, "http_get return code: %d", ret);
//            for(int i = 0; i < ACL_BUF_SIZE && acl_buf[i] != 0; i++) {
//                putchar(acl_buf[i]);
//            }
            
            vPortFreeTagged(acl_buf);
            ESP_LOGI(TAG, "Free heap size after free = %zu", xPortGetFreeHeapSizeCaps(MALLOC_CAP_8BIT));
        } else {
            ESP_LOGE(TAG, "Could not malloc acl buffer");
        }
        

        
        for(int countdown = 30; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "%d...", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}