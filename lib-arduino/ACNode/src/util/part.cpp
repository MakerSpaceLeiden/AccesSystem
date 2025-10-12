// See https://github.com/espressif/esp-idf/blob/master/examples/storage/partition_api/partition_find/README.md
//
#include <Arduino.h>
#include <string.h>
#include <assert.h>
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static void partloop(Print &out, const char * label, esp_partition_type_t part_type) {
      const esp_partition_t * running = esp_ota_get_running_partition();
      const esp_partition_t * boot = esp_ota_get_boot_partition();
      const esp_partition_t * nxt = esp_ota_get_next_update_partition(NULL);
      const esp_partition_t *next_partition = NULL;
      esp_partition_iterator_t iterator = NULL;

      iterator = esp_partition_find(part_type, ESP_PARTITION_SUBTYPE_ANY, NULL);

      while (iterator) {
         next_partition = esp_partition_get(iterator);
         if (next_partition != NULL) {
            out.printf("%5s %2x addr: 0x%06x; size: 0x%06x; label: %s%s%s%s\n", 
		label, part_type, next_partition->address, next_partition->size, next_partition->label,
		(next_partition == running) ? " RUNNING" : "",
		(next_partition == boot) ? " BOOT" : "",
		(next_partition == nxt) ? " NEXT" : ""
	    );
         iterator = esp_partition_next(iterator);
        }
      }
      esp_partition_iterator_release(iterator);
}

void partition_info(Print &out) {
        out.println("Partition list:");
        partloop(out, "APP ",ESP_PARTITION_TYPE_APP);
        partloop(out, "DATA",ESP_PARTITION_TYPE_DATA);
}
   
String currentPartition() {
	const esp_partition_t * running = esp_ota_get_running_partition();
	if (running  && running->label)
		return String(running->label);
	return String("Unset");
}

size_t get_rom_size() {
   static size_t total = 0;
   if (total == 0) {
      ESP_LOGI("ROM", "Partition table\n=====\n");

      esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
      for (; it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI("ROM", "\tPartition '%s' at offset 0x%" PRIx32 " type %x, size 0x%" PRIx32, part->label, part->address, part->type, part->size);
       	total += part->size;
      };
      esp_partition_iterator_release(it);
    };
    return total;
};
