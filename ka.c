#include "ka.h"
#include "lm.h"
#include "kl.h"
#include "kr.h"
#include "dk.h"
#include "scancodes.h"

/* ka.c -- key actions
 *
 * Contains implementation of all the actions that could
 * be assigned to keys, either globally, per-language, or per-application.
 * Makes the actions accessible by name.
 *
 * */

#define ka(name) void name(KA_PARAMS)

ka(KA_compose) { DK_dkn(0, down, sc); }
#define dkn(i) ka(KA_dkn_##i) { DK_dkn(i, down, sc); }
# include "ka_dk.h"
#undef dkn

ka(KA_toggle) {
    if (!down)
        return;
    KL_toggle();
}

ka(KA_restart) {
    if (!down)
        return;
    restart_the_program();
}

ka(KA_next_layout) {
    if (!down)
        return;
    LM_activate_next_locale();
}

ka(KA_prev_layout) {
    if (!down)
        return;
    LM_activate_prev_locale();
}

ka(KA_control) {
    KM_shift_event(&KL_km_control, down, sc);
    if (!(!down && KL_km_control.shifts_count > 0))
        keybd_event(VK_LCONTROL, SC_LCONTROL, (down ? 0 : KEYEVENTF_KEYUP), 0);
}

ka(KA_l5_shift) {
    KL_phys_mods[sc] = KLM_PHYS_TEMP;
    KM_shift_event(&KL_km_l5, down, sc);
}

ka(KA_l5_lock) {
    KM_lock_event(&KL_km_l5, down, sc);
}

ka(KA_l3_latch) {
    KM_latch_event(&KL_km_l3, down, sc);
}

ka(KA_l2_latch) {
    KL_km_shift.latch_faked = VK_LSHIFT;
    KM_latch_event(&KL_km_shift, down, sc);
}

ka(KA_kr_toggle) {
    if (!down)
        return;
    KR_toggle();
}

ka(KA_kr_on_pt) {
    if (down) {
        printf("kr_on_pt(%d,%d) ", KR_active, (int)KR_id);
        if (!KR_active || !KR_id) {
            KR_activate();
        }
        keybd_event(0, sc, KEYEVENTF_SCANCODE, 0);
    } else {
        keybd_event(0, sc, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);
    }
}

ka(KA_kr_off_pt) {
    if (down) {
        if (KR_active) {
            KR_clear();
            KR_resume(true);
        }
        keybd_event(0, sc, KEYEVENTF_SCANCODE, 0);
    } else {
        keybd_event(0, sc, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);
    }
}

ka(KA_kr_off) {
    if (down) {
        if (KR_active) {
            KR_clear();
            KR_resume(true);
        }
    }
}

ka(KA_dim_screen) {
    if (!down)
        return;
    printf("dim_screen ");
    Sleep(500);
    SendMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)2);
}

ka(KA_close_window) {
    if (!down)
        return;
    printf("close_window ");
    SendMessage(GetForegroundWindow(), WM_CLOSE, 0, 0);
}

ka(KA_toggle_on_top) {
    if (!down)
        return;
    HWND hwnd = GetForegroundWindow();
    WINDOWINFO wi;
    GetWindowInfo(hwnd, &wi);
    bool topmost = wi.dwExStyle & WS_EX_TOPMOST;
    printf("top{%d} ", topmost);
    RECT r;
    GetWindowRect(hwnd, &r);
    SetWindowPos(hwnd, (topmost ? (HWND)HWND_NOTOPMOST : (HWND)HWND_TOPMOST), r.left, r.top, r.right - r.left, r.bottom - r.top, 0);
}

#undef ka

typedef struct {
    KA_FUNC func;
    char* name;
} KA_Pair;

#define ka(name) { name, (char*)#name }
KA_Pair KA_fns[] = {
    ka(KA_compose),
    #define dkn(i) ka(KA_dkn_##i),
    # include "ka_dk.h"
    #undef dkn

    ka(KA_toggle),
    ka(KA_restart),
    ka(KA_next_layout),
    ka(KA_prev_layout),

    ka(KA_control),
    ka(KA_l5_shift),
    ka(KA_l5_lock),
    ka(KA_l3_latch),
    ka(KA_l2_latch),

    ka(KA_kr_toggle),
    ka(KA_dim_screen),
    ka(KA_close_window),
    ka(KA_toggle_on_top),
    ka(KA_kr_on_pt),
    ka(KA_kr_off_pt),
    ka(KA_kr_off),
};
#undef ka

void KA_update_dk_names(void) {
    size_t i;
    fori (i, 1, KA_dkn_count) {
        char *name = DK_index_to_name(i);
        if (!name) { break; }
        KA_fns[i].name = name;
    }
}

int KA_call(UINT id, KA_PARAMS) {
    if (id >= lenof(KA_fns))
        return -1;
    KA_fns[id].func(down, sc);
    return 0;
}

int KA_name_to_id(char *name) {
    UINT i;
    fori (i, 0, lenof(KA_fns)) {
        KA_Pair *p = KA_fns + i;
        if (!strcmp(p->name, name)) {
            return i;
        }
    }
    return -1;
}

void KA_init() {
    UINT i;
    fori (i, 0, lenof(KA_fns)) {
        KA_Pair *ka_pair = KA_fns + i;
        ka_pair->name += 3;
        printf(" ka%d{%p,%s}", i, ka_pair->func, ka_pair->name);
    }
    puts("");
}
