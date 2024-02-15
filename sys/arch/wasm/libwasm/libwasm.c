
#include <sys/types.h>
#include <sys/null.h>

/**
 * Returns a boolean value that determines if the two wasm types are equal.
 *
 * Both ptr1 and ptr2 should point to raw WebAssembly data struct representing types.
 */
bool lwa_type_is_equal(const char *ptr1, const char *ptr2)
{
    uint32_t cnt1;
    uint32_t cnt2;
    uint32_t blen = 0;

    if (*(ptr1++) != 0x60 || *(ptr2++) != 0x60) {
        return false;
    }

    cnt1 = decodeULEB128((uint8_t *)ptr1, &blen, NULL, NULL);
    ptr1 += blen;
    cnt2 = decodeULEB128((uint8_t *)ptr2, &blen, NULL, NULL);
    ptr2 += blen;

    if (cnt1 != cnt2) {
        return false;
    }

    for (int i = 0; i < cnt1; i++) {
        if (*(ptr1++) != *(ptr2++)) {
            return false;
        }
    }
    
    cnt1 = decodeULEB128((uint8_t *)ptr1, &blen, NULL, NULL);
    ptr1 += blen;
    cnt2 = decodeULEB128((uint8_t *)ptr2, &blen, NULL, NULL);
    ptr2 += blen;

    if (cnt1 != cnt2) {
        return false;
    }

    for (int i = 0; i < cnt1; i++) {
        if (*(ptr1++) != *(ptr2++)) {
            return false;
        }
    }


    return true;
}