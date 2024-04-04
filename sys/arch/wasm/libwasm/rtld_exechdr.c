
#include <string.h>

#include "libwasm.h"
#include "wasmloader.h"


struct wasm_exechdr_secinfo *
_rtld_exechdr_find_section(struct wash_exechdr_rt *exehdr, int sectype, const char *secname)
{
    struct wasm_exechdr_secinfo *sec;
    uint32_t namesz, count;

    if (exehdr->section_cnt == 0 || exehdr->secdata == NULL) {
        return NULL;
    }

    if (sectype == WASM_SECTION_CUSTOM) {

        if (secname == NULL) {
            return NULL;
        }

        namesz = strlen(secname);

        sec = exehdr->secdata;
        count = exehdr->section_cnt;
        for (int i = 0; i < count; i++) {
            if (sec->wasm_type == WASM_SECTION_CUSTOM && sec->namesz == namesz && strncmp(sec->name, secname, namesz) == 0) {
                return sec;
            }
            sec++;
        }

    } else {

        sec = exehdr->secdata;
        count = exehdr->section_cnt;
        for (int i = 0; i < count; i++) {
            if (sec->wasm_type == sectype) {
                return sec;
            }
            sec++;
        }
    }

    return NULL;
}