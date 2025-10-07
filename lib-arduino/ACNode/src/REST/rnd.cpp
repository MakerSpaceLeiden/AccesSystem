#include "ACNode.h"
#include <cstring>

#include "esp_random.h"
#include "mbedtls/error.h"

#include "rnd.h"

mbedtls_entropy_context entropy_ctx;
mbedtls_ctr_drbg_context ctr_drbg, *p_ctr_drbg;

const char TAG[] ="rnd";
static const char seed[] = "XXXX" __DATE__ __TIME__;

void ensure_rnd() {
    if (p_ctr_drbg != NULL)
        return;

    mbedtls_entropy_init( &entropy_ctx);
    mbedtls_ctr_drbg_init( &ctr_drbg);

    *(uint32_t*)seed = esp_random();

    int ret;
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                               &entropy_ctx, (const unsigned char *) seed, strlen(seed))) < 0) {
	char buf[256];
	mbedtls_strerror( ret, buf, sizeof(buf) ); \
	ESP_LOGE(TAG, "ensure_rnd() failed. returned -x%02x, %s", (unsigned int) - ret, buf); \
        return;
    };

    p_ctr_drbg = &ctr_drbg;
};

