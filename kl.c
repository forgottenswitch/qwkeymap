#include "kl.h"
#include "ka.h"
#include "km.h"
#include "lm.h"
#include "dk.h"
#include "scancodes.h"
#include "stdafx.h"
#ifndef NOGUI
# include "ui.h"
#endif // NOGUI

/* kl.c -- the low level keyboard hook
 *
 * Allows for key presses and releases to be intercepted,
 * and then either be blocked, passed, or altered.
 * Requires Windows 95.
 * The interception is toggled, and when off, the program does nothing.
 *
 * The alteration is performed using the lookup table,
 * which is a compilation of global, language-specific,
 * and application-specific tables.
 *
 * This file also contains all the table manipulation functions.
 *
 *  */

bool KL_active = false;
HHOOK KL_handle;

KM KL_km_shift;
KM KL_km_control;
KM KL_km_alt;
KM KL_km_win;
KM KL_km_l3;
KM KL_km_l5;

KLY KL_kly;

/* vk/sc-to-wchar tables */
typedef WCHAR KLVKW[2][VK_COUNT];
typedef WCHAR KLSCW[2][SC_COUNT];
KLVKW KL_vkw;
KLSCW KL_scw;

UCHAR KL_phys[SC_COUNT];
UCHAR KL_phys_mods[SC_COUNT];

VK KL_mods_vks[SC_COUNT];
bool KL_dk_in_effect;

void KL_activate() {
    KL_handle = SetWindowsHookEx(WH_KEYBOARD_LL, KL_proc, OS_current_module_handle(), 0);
    if (KL_handle == NULL) {
        puts("\n\nSetWindowsHookEx failed!");
        OS_print_last_error();
        puts("\n");
    }
    KL_active = true;
#ifndef NOGUI
    UI_TR_update();
#endif // NOGUI
}

void KL_deactivate() {
    UnhookWindowsHookEx(KL_handle);
    KL_active = false;
#ifndef NOGUI
    UI_TR_update();
#endif // NOGUI
}

void KL_toggle() {
    if (KL_active) {
        KL_deactivate();
    } else {
        KL_activate();
    }
}

void KL_dk_on_sc(SC sc, int level) {
    printf("{dk sc%03x}", sc);
    WCHAR wc = ((unsigned short)(sc) >= KPN) ? 0 : KL_scw[level % 2][sc];
    printf("{dk l%d '%c'}", level % 2, (char)wc);
    if (wc) { KL_dk_in_effect = DK_on_char(wc); }
    else { KL_dk_in_effect = 0; }
}

void KL_dk_on_vk(VK vk, int level, BOOL need_shift) {
    if (need_shift) {
        level = 1;
    }
    printf("{dk vk%02x}", vk);
    WCHAR wc = KL_vkw[level % 2][(unsigned char)(vk)];
    printf("{dk l%d '%c'}", level % 2, (char)wc);
    if (wc) { KL_dk_in_effect = DK_on_char(wc); }
    else { KL_dk_in_effect = 0; }
}

void KL_dk_on_wchar(WCHAR wc) {
    printf("{dk u%04x}", wc);
    if (wc) { KL_dk_in_effect = DK_on_char(wc); }
    else { KL_dk_in_effect = 0; }
}

void KL_dk_send_wchar(WCHAR wc) {
    KL_dk_in_effect = 0;
    INPUT inp;
    inp.type = INPUT_KEYBOARD;
    inp.ki.wVk = 0;
    inp.ki.dwFlags = KEYEVENTF_UNICODE;
    inp.ki.dwExtraInfo = 0;
    inp.ki.wScan = wc;
    inp.ki.time = GetTickCount();
    SendInput(1, &inp, sizeof(INPUT));;
}

#define duch() (down ? '_' : '^')
#define frch() (faked ? 'F' : 'R')

#define RawThisEvent() 0
#define StopThisEvent() 1
#define PassThisEvent() CallNextHookEx(NULL, aCode, wParam, lParam)
LRESULT CALLBACK KL_proc(int aCode, WPARAM wParam, LPARAM lParam) {
    if (aCode != HC_ACTION) { return PassThisEvent(); }

    /* Gather the values of interest */
    PKBDLLHOOKSTRUCT ev = (PKBDLLHOOKSTRUCT) lParam;
    DWORD flags = ev->flags;
    SC sc = (SC) ev->scanCode;
    //VK vk = (VK) ev->vkCode;
    bool down = (wParam != WM_KEYUP && wParam != WM_SYSKEYUP);
    // non-physical key events:
    //   injected key presses (generated by programs - keybd_event(), SendInput()),
    //   their (non-injected) release counterparts,
    //   fake shift presses and releases by driver accompanying numpad keys
    //     (so that numlock'ed keys are independent of shift state yet have the same 2 levels),
    //   LControl press/release by OS (the window system) triggered by AltGr RAlt event
    // Only check here for injected presses and corresponding releases.
    bool faked;
    faked = (flags & LLKHF_INJECTED || (!(KL_phys[sc]) && !down));
    printf("{sc%03xvk%02lx%c%c}", sc, ev->vkCode, frch(), duch());

    /* Track the modifiers state: shift, control, alt, win, level3, level5
     * (the latter two are not present in the OS)
     *  */
    if (!faked) {
        KL_phys[sc] = down;
        BYTE mod = KL_phys_mods[sc];
        if (mod) {
            switch (mod) {
            case MOD_SHIFT:
                printf("{Shift%c} ", duch());
                KM_shift_event(&KL_km_shift, down, sc);
                break;
            case MOD_CONTROL:
            printf("{Ctrl%c} ", duch());
                KM_shift_event(&KL_km_control, down, sc);
                break;
            case MOD_ALT:
            printf("{Alt%c} ", duch());
                KM_shift_event(&KL_km_alt, down, sc);
                break;
            case MOD_WIN:
            printf("{Win%c} ", duch());
                KM_shift_event(&KL_km_win, down, sc);
                break;
            case KLM_PHYS_TEMP:
                KL_phys_mods[sc] = 0;
                goto mods_end;
            }
            return PassThisEvent();
        } else {
            KM_nonmod_event(&KL_km_shift, down, sc);
            KM_nonmod_event(&KL_km_l3, down, sc);
        }
    }
    mods_end:

    /* Set the 'extended' bit in scancode if it should be set */
    if (flags & LLKHF_EXTENDED) {
        sc |= 0x100;
    }

    /* Do not process any simulated key events,
     * or events which most likely are hardware-dependent
     * (such as multimedia buttons, power buttons, etc.);
     * just let them through.
     * */
    if (faked || sc >= KPN) {
        if (!faked) {
            printf("{sc%03lx,vk%02lx%c} ", ev->scanCode, ev->vkCode, (down ? '_' : '^'));
        }
        return PassThisEvent();
    }

    /* Compute the layout level currently in effect
     * (using the modifier tracking data) */
    int lv = 0;
    if (KL_km_l3.in_effect) {
        lv = 2;
    } else if (KL_km_l5.in_effect) {
        lv = 4;
    }
    if (KL_km_shift.in_effect) {
        lv += 1;
    }
    
    /* Acquire the key binding (the alteration to be performed) */
    LK lk = KL_kly[lv][sc];
    printf(" l%d,b%x", lv, lk.binding);

    /* If any of control, alt, or win is currently in effect,
     * alter the key being send using the special when-a-modifier-is-in-effect
     * lookup table, except when this would cancel a key action.
     * */
    if (lv <= 1 && (KL_km_alt.in_effect || KL_km_control.in_effect || KL_km_win.in_effect)) {
        VK vk = KL_mods_vks[sc];
        /* the when-modifier alteration */
        if (vk && !(lk.active && lk.mods && KLM_KA)) {
            keybd_event(vk, 0, (down ? 0 : KEYEVENTF_KEYUP), 0);
            printf(" SendVK%c(%02x=%c)", (down ? '_' : '^'), vk, vk);
            return StopThisEvent();
        }
    }

    /* Processing of key when in dead key sequence */
    #define MaybeDeadKeyVK() \
        if (KL_dk_in_effect) { \
            if (down) { KL_dk_on_vk(ev->vkCode, lv, lk.mods & MOD_SHIFT); }; \
            return StopThisEvent(); \
        };

    /* If there is no alteration ... */
    if (!lk.active) {
        printf(" na%s", (down ? "_ " : "^\n"));
        if (lv < 2) { /* ... just let the event fire when none or shift is in effect */
            printf("[12]");
            MaybeDeadKeyVK();
            return PassThisEvent();
        } else if (lv < 4) { /* ... when level3 is in effect ... */
            printf("[34]");
            INPUT inp[5], *curinp = inp;
            char lctrl = (KL_phys[SC_LCONTROL] ? 0 : 1), lctrl1=-lctrl;
            char ralt = (KL_phys[SC_RMENU] ? 0 : 1), ralt1=-ralt;
            size_t inpl = lctrl*2 + ralt*2;
            MaybeDeadKeyVK();
            if (!inpl) { /* ... just let the event fire when none or shift is in effect */
                return PassThisEvent();
            }
            /* ... release all controls and alts while sending the event otherwise
             * (this is not to let OS use AltGr binding (its own level3/4) if any,
             * as it would not be appropriate)
             * */
            inpl++;
            size_t i;
            DWORD time = GetTickCount();
            fori (i, 0, inpl) {
                bool down1;
                SC sc1;
                if (lctrl) {
                    down1 = lctrl > 0;
                    sc1 = SC_LCONTROL;
                    lctrl = 0;
                } else if (ralt) {
                    down1 = ralt > 0;
                    sc1 = SC_RMENU;
                    ralt = 0;
                } else {
                    lctrl = lctrl1;
                    ralt = ralt1;
                    sc1 = sc;
                    down1 = down;
                }
                curinp->type = INPUT_KEYBOARD;
                curinp->ki.wVk = 0;
                curinp->ki.dwFlags = KEYEVENTF_SCANCODE | (down1 ? 0 : KEYEVENTF_KEYUP);
                curinp->ki.dwExtraInfo = 0;
                curinp->ki.wScan = sc1;
                curinp->ki.time = time;
                curinp++;
            }
            SendInput(inpl, inp, sizeof(INPUT));
            return StopThisEvent();
        /* ... block the event if level5 is in effect */
        } else {
            printf("[56]");
            return StopThisEvent();
        }
    }
    #undef MaybeDeadKeyVK

    /* If there is an alteration,
     * determine its type:
     *   scancode (KLM_SC),
     *   UCS-2 character (KLM_WCHAR),
     *   key action from ka.c (KLM_KA),
     *   virtual keycode (none of the above)
     *  */
    UCHAR mods = lk.mods;
    if (mods == KLM_SC) { /* ... if scancode, just alter the scancode */
        printf("%csc%03lx=%02x ", (down ? '_' : '^'), ev->scanCode, lk.binding);
        /* processing of scan code when in dead key sequence */
        if (KL_dk_in_effect) {
            if (down) { KL_dk_on_sc(lk.binding, lv); };
        } else {
            keybd_event(0, lk.binding, KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP), 0);
        }
        return StopThisEvent();
    } else if (mods == KLM_WCHAR) { /* ... if a character, SendInput it */
        if (down) {
            WCHAR wc = lk.binding;
            printf(" send U+%04x ", wc);
            /* processing of character when in dead key sequence */
            if (KL_dk_in_effect) {
                KL_dk_on_wchar(wc);
            } else {
                INPUT inp;
                inp.type = INPUT_KEYBOARD;
                inp.ki.wVk = 0;
                inp.ki.dwFlags = KEYEVENTF_UNICODE;
                inp.ki.dwExtraInfo = 0;
                inp.ki.wScan = wc;
                inp.ki.time = GetTickCount();
                SendInput(1, &inp, sizeof(INPUT));
            }
        }
    } else if (mods & KLM_KA) { /* ... if key action, call it */
        printf(" ka_call ka%d(%d){", lk.binding, down);
        if (lk.binding < KA_dkn_count) {
            KL_dk_in_effect = true;
        }
        KA_call(lk.binding, down, sc);
        printf("}%s", (down ? "_" : "^\n"));
    }
    /* ... if virtual key code, temporarily bring shift, control and alt
     * into the state required by alteration, and send the virtual key code.
     * */
    /* processing of virtual keycode when in dead key sequence */
    else if (KL_dk_in_effect) {
        if (down) { KL_dk_on_vk(lk.binding, lv, lk.mods & MOD_SHIFT); };
    }
    else {
        bool shift_was_down = KL_km_shift.in_effect;
        bool need_shift = (mods & MOD_SHIFT);
        char mod_shift = (KL_km_shift.in_effect ? (need_shift ? 0 : -1) : (need_shift ? 1 : 0)), mod_shift0 = mod_shift;
        char mod_control = (((mods & MOD_CONTROL) && !KL_km_control.in_effect) ? 1 : 0), mod_control0 = mod_control;
        char mod_alt = (((mods & MOD_ALT) && !KL_km_alt.in_effect) ? 1 : 0), mod_alt0 = mod_alt;
        int mods_count = (mod_shift & 1) + mod_control + mod_alt;
        if (!mods_count) {
            printf(" evt vk%02x%c", lk.binding, (down ? '_' : '^'));
            keybd_event(lk.binding, sc, (down ? 0 : KEYEVENTF_KEYUP), 0);
        } else {
            int inps_count = 1 + mods_count * 2;
            INPUT inps[7];
            int i, tick_count = GetTickCount();
            printf(" send%d vk%02x[%d%d%d]%s", inps_count, lk.binding, mod_shift, mod_control, mod_alt, (down ? "_" : "^\n"));
            fori (i, 0, inps_count) {
                VK vk1 = lk.binding;
                DWORD flags = 0;
                if (mod_shift) {
                    if (mod_shift > 0 && !need_shift && !shift_was_down) {
                        inps_count--;
                        continue;
                    }
                    printf("+%d-", mod_shift);
                    flags = (mod_shift > 0 ? 0 : KEYEVENTF_KEYUP);
                    mod_shift = 0;
                    vk1 = VK_LSHIFT;
                } else if (mod_control) {
                    printf("^");
                    flags = (mod_control > 0 ? 0 : KEYEVENTF_KEYUP);
                    mod_control = 0;
                    vk1 = VK_LCONTROL;
                } else if (mod_alt) {
                    printf("!");
                    flags = (mod_alt > 0 ? 0 : KEYEVENTF_KEYUP);
                    mod_alt = 0;
                    vk1 = VK_RMENU;
                } else {
                    printf("-");
                    mod_shift = -mod_shift0;
                    mod_control = -mod_control0;
                    mod_alt = -mod_alt0;
                    vk1 = lk.binding;
                    flags = (down ? 0 : KEYEVENTF_KEYUP);
                }
                INPUT *inp = &(inps[i]);
                inp->type = INPUT_KEYBOARD;
                inp->ki.wVk = vk1;
                inp->ki.dwFlags = flags;
                inp->ki.dwExtraInfo = 0;
                inp->ki.wScan = sc;
                inp->ki.time = tick_count;
            }
            SendInput(inps_count, inps, sizeof(INPUT));
        }
    }

    return StopThisEvent();
}
#undef RawThisEvent
#undef StopThisEvent
#undef PassThisEvent

KLY *KL_bind_kly = &KL_kly;

void KL_bind(SC sc, UINT lvl, UINT mods, SC binding) {
    LK lk;
    SC binding1 = binding;
    UINT lvl1 = lvl+1;
    if (mods & KLM_SC) {
        binding1 = OS_sc_to_vk(binding);
    }
    printf("bind sc%03x[%d]:", sc, lvl1);
    if (mods & KLM_WCHAR) {
        printf("u%04x ", binding);
    } else if (mods & KLM_KA) {
        printf("ka%d ", binding);
    } else {
        if (mods & KLM_SC) {
            printf("sc%03x=>vk%02x ", binding, binding1);
        } else {
            printf("vk%02x ", binding);
        }
    }
    if (!(lvl1 % 2) && !(mods & KLM_WCHAR) && !(mods & KLM_KA)) {
        printf("+(%x)", mods);
        mods |= MOD_SHIFT;
    }
    lk.active = true;
    lk.mods = mods;
    lk.binding = binding1;
    (*KL_bind_kly)[lvl][sc] = lk;
}

void KL_temp_sc(SC sc, SC mods, SC binding) {
    if (mods == KLM_KA) {
        printf("t sc%03x=ka%d ", sc, binding);
    } else if (mods == KLM_SC) {
        printf("t sc%03x=%02x ", sc, binding);
    } else {
        printf("t sc%03x={%02x/%02x} ", sc, binding, mods);
    }
    LK lk = { true, (UCHAR)mods, binding };
    KL_kly[0][sc] = lk;
    KL_kly[1][sc] = lk;
}

typedef struct {
    bool compiled;
    // Primary Language ID
    LANGID lang;
    // Whether defines VKs for while modifiers are in effect
    bool vks_lang;
    // Bindings
    KLY kly;
    // vk/sc-to-wchar tables
    KLVKW vkw;
    KLSCW scw;
} KLC;

size_t KL_klcs_size = 0;
size_t KL_klcs_count = 0;
KLC *KL_klcs;

void KL_add_lang(LANGID lang) {
    if ((KL_klcs_count+=1) > KL_klcs_size) {
        KL_klcs = (KLC*) realloc(KL_klcs, (KL_klcs_size *= 1.5) * sizeof(KLC));
    }
    KLC *klc = KL_klcs + KL_klcs_count - 1;
    klc->compiled = false;
    klc->lang = lang;
    klc->vks_lang = false;
}

KLY *KL_lang_to_kly(LANGID lang) {
    UINT i;
    fori(i, 0, KL_klcs_count) {
        if (KL_klcs[i].lang == lang)
            return &(KL_klcs[i].kly);
    }
    return NULL;
}

KLC *KL_lang_to_klc(LANGID lang) {
    UINT i;
    printf("klcs_count:%d ", KL_klcs_count);
    fori(i, 0, KL_klcs_count) {
        printf("klc.lang:%x ", KL_klcs[i].lang);
        if (KL_klcs[i].lang == lang)
            return &(KL_klcs[i]);
    }
    return NULL;
}

LANGID KL_vks_lang = LANG_NEUTRAL;
BYTE KL_vksc_kbdstate[256];

void KL_compile_klc(KLC *klc) {
    if (klc->lang == LANG_NEUTRAL)
        return;
    HKL cur_hkl = GetKeyboardLayout(0);
    ActivateKeyboardLayout(LM_langid_to_hkl(klc->lang), 0);
    KLY *kly = &(klc->kly);
    CopyMemory(KL_kly, KL_lang_to_kly(LANG_NEUTRAL), sizeof(KLY));
    bool vks_lang = klc->vks_lang;
    int lv, sc;
    fori(lv, 0, KLVN) {
        fori(sc, 0, KPN) {
            LK lk = (*kly)[lv][sc];
            if (lk.active) {
                printf("sc%03x:%d ", sc, lv+1);
                KL_kly[lv][sc] = lk;
            }
        }
    }
    fori (lv, 0, KLVN) {
        fori (sc, 0, KPN) {
            LK *p_lk = &(KL_kly[lv][sc]), lk = *p_lk;
            if (lk.active) {
                //printf("a(%03x:%d:%x/%x)", sc, lv+1, lk.binding, lk.mods);
                if (lk.mods & KLM_WCHAR) {
                    WCHAR w = lk.binding;
                    printf("sc%03x'%c':%d->", sc, w, lv+1);
                    KP kp = OS_wchar_to_vk(w);
                    if (kp.vk != 0xFF) {
                        printf("vk%02x/%d", kp.vk, kp.mods);
                        bool same_sc = (kp.mods == KLM_SC && kp.sc == sc);
                        bool same_vk = (lv <= 1 && kp.mods == (MOD_SHIFT * (lv % 2)) && kp.vk == OS_sc_to_vk(sc));
                        if (same_sc || same_vk) {
                            lk.active = 0;
                            lk.binding = 0;
                            lk.mods = 0;
                        } else {
                            if (lv == 0 && vks_lang && !(kp.mods & KLM_SC)) {
                                printf("_M");
                                KL_mods_vks[sc] = kp.vk;
                            }
                            lk.mods = kp.mods;
                            lk.binding = kp.vk;
                        }
                    } else {
                        printf("u%04x", w);
                    }
                    printf(";");
                }
                *p_lk = lk;
            }
        }
    }
    void *to_wc_cache = OS_ToUnicodeThroghVkKeyScan_new_cache();
    fori (lv, 0, 2) {
        BOOL shift_down = lv % 2;
        /* Set the shift state according to level */
        if (shift_down) {
            KL_vksc_kbdstate[VK_SHIFT] = 1;
            KL_vksc_kbdstate[VK_LSHIFT] = 1;
            KL_vksc_kbdstate[VK_RSHIFT] = 1;
        } else {
            KL_vksc_kbdstate[VK_SHIFT] = 0;
            KL_vksc_kbdstate[VK_LSHIFT] = 0;
            KL_vksc_kbdstate[VK_RSHIFT] = 0;
        }
        int vk;
        printf("\n");
        fori (vk, 0, VK_COUNT) {
            WCHAR wc = 0;
            /* Translate virtual keycode and shift state into UCS-2 character.
             * Do not use ToUnicode(), as it ignores shift state.
             * */
            // WCHAR buf[4] = { 0, 0, 0, 0 };
            // int n = ToUnicode(vk, 0, KL_vksc_kbdstate, buf, lenof(buf), 0);
            // if (n == 1) { wc = buf[0]; };
            wc = OS_ToUnicodeThroghVkKeyScan(to_wc_cache, vk, shift_down);
            printf("[vk%02x%cu%02x]", vk, lv ? '+' : ' ', wc);
            KL_vkw[lv][vk] = wc;
        }
        printf("\n");
        fori (sc, 0, KPN) {
            WCHAR wc = 0;
            /* Translate scancode and shift state into UCS-2 character. */
            VK vk = OS_sc_to_vk(sc);
            // WCHAR buf[4] = { 0, 0, 0, 0 };
            // int n = ToUnicode(vk, 0, KL_vksc_kbdstate, buf, lenof(buf), 0);
            // if (n == 1) { wc = buf[0]; }
            wc = OS_ToUnicodeThroghVkKeyScan(to_wc_cache, vk, shift_down);
            printf("[sc%03x=>vk%02x%cu%02x]", sc, vk, lv ? '+' : ' ', wc);
            KL_scw[lv][sc] = wc;
        }
    }
    free(to_wc_cache);
    CopyMemory(kly, KL_kly, sizeof(KLY));
    CopyMemory(&(klc->vkw), KL_vkw, sizeof(KLVKW));
    CopyMemory(&(klc->scw), KL_scw, sizeof(KLSCW));
    klc->compiled = true;
    ActivateKeyboardLayout(cur_hkl, 0);
}

void KL_activate_lang(LANGID lang) {
    printf("lang %04x ", lang);
    KLC *lang_klc = KL_lang_to_klc(lang);
    if (lang_klc == NULL) {
        printf("no such lang! ");
        return;
    }
    if (lang_klc->compiled) {
        CopyMemory(KL_kly, lang_klc->kly, sizeof(KLY));
        CopyMemory(KL_vkw, lang_klc->vkw, sizeof(KLVKW));
        CopyMemory(KL_scw, lang_klc->scw, sizeof(KLSCW));
    } else {
        printf("compile ");
        KL_compile_klc(lang_klc);
    }
    puts("");
}

void KL_set_bind_lang(LANGID lang) {
    KLY *kly = KL_lang_to_kly(lang);
    if (kly == NULL) {
        KL_add_lang(lang);
        kly = KL_lang_to_kly(lang);
    }
    KL_bind_kly = kly;
}

void KL_set_vks_lang(LANGID lang) {
    KL_vks_lang = lang;
}

void KL_define_one_vk(VK vk, KLY *kly) {
    SC sc = OS_vk_to_sc(vk);
    if (!sc) {
        return;
    }
    LK lk = (*kly)[0][sc];
    if (lk.active && lk.mods == 0) {
        vk = lk.binding;
    }
    printf(" DefineVK(%02x, sc%03x)", vk, sc);
    KL_mods_vks[sc] = vk;
}

void KL_define_vks() {
    KLC *klc = KL_lang_to_klc(KL_vks_lang);
    KLY *kly = &(klc->kly);
    klc->vks_lang = true;
    if (!klc->compiled) {
        KL_compile_klc(klc);
    }
    int vk;
    // 0..9
    fori (vk, 0x30, 0x3A) {
        KL_define_one_vk(vk, kly);
    }
    // A..Z
    fori (vk, 0x41, 0x5B) {
        KL_define_one_vk(vk, kly);
    }
    // Tilde, comma, period, brackets, semicolon, quote, slash
    KL_define_one_vk(VK_OEM_1, kly);
    KL_define_one_vk(VK_OEM_2, kly);
    KL_define_one_vk(VK_OEM_3, kly);
    KL_define_one_vk(VK_OEM_4, kly);
    KL_define_one_vk(VK_OEM_5, kly);
    KL_define_one_vk(VK_OEM_6, kly);
    KL_define_one_vk(VK_OEM_7, kly);
    KL_define_one_vk(VK_OEM_8, kly);
}

void KL_bind_init() {
    KL_bind_kly = KL_lang_to_kly(LANG_NEUTRAL);
}

void KL_init() {
    ZeroBuf(KL_kly);
    ZeroBuf(KL_phys);
    KL_klcs = (KLC*)calloc((KL_klcs_size = 4), sizeof(KLC));

    KM_init(&KL_km_shift);
    KM_init(&KL_km_control);
    KM_init(&KL_km_alt);
    KM_init(&KL_km_l3);
    KM_init(&KL_km_l5);

    KL_add_lang(LANG_NEUTRAL);
    KL_bind_kly = KL_lang_to_kly(LANG_NEUTRAL);
    UINT sc;
    fori (sc, 0, lenof(KL_phys_mods)) {
        VK vk = OS_sc_to_vk(sc);
        UCHAR mod = 0;
        switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            mod = MOD_SHIFT;
            break;
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            mod = MOD_CONTROL;
            break;
        case VK_MENU: case VK_LMENU: case VK_RMENU:
            mod = MOD_ALT;
            break;
        case VK_LWIN: case VK_RWIN:
            mod = MOD_WIN;
            break;
        }
        if (mod) {
            printf("[mods] sc%03x => vk%02x, mod %x\t", sc, vk, mod);
        }
        KL_phys_mods[sc] = mod;
    }
#define mod_vk(mod, vk) do { sc = OS_vk_to_sc(vk); if (sc) { printf("[mods] sc%03x => vk%02x, mod %x\t", sc, vk, mod); KL_phys_mods[sc] = mod; }; } while (0)
    mod_vk(MOD_WIN, VK_LWIN);
    mod_vk(MOD_WIN, VK_RWIN);
#undef mod_vk
}
