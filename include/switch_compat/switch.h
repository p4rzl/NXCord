#pragma once

#include_next <switch.h>

#ifndef PACKED
#define PACKED NX_PACKED
#endif

typedef enum {
    CONTROLLER_PLAYER_1 = 0,
    CONTROLLER_PLAYER_2 = 1,
    CONTROLLER_PLAYER_3 = 2,
    CONTROLLER_PLAYER_4 = 3,
    CONTROLLER_PLAYER_5 = 4,
    CONTROLLER_PLAYER_6 = 5,
    CONTROLLER_PLAYER_7 = 6,
    CONTROLLER_PLAYER_8 = 7,
    CONTROLLER_HANDHELD = 8,
    CONTROLLER_UNKNOWN  = 9,
    CONTROLLER_P1_AUTO  = 10,
} HidControllerID;

typedef enum {
    JOYSTICK_LEFT       = 0,
    JOYSTICK_RIGHT      = 1,
    JOYSTICK_NUM_STICKS = 2,
} HidControllerJoystick;

typedef enum {
    KEY_A            = HidNpadButton_A,
    KEY_B            = HidNpadButton_B,
    KEY_X            = HidNpadButton_X,
    KEY_Y            = HidNpadButton_Y,
    KEY_LSTICK       = HidNpadButton_StickL,
    KEY_RSTICK       = HidNpadButton_StickR,
    KEY_L            = HidNpadButton_L,
    KEY_R            = HidNpadButton_R,
    KEY_ZL           = HidNpadButton_ZL,
    KEY_ZR           = HidNpadButton_ZR,
    KEY_PLUS         = HidNpadButton_Plus,
    KEY_MINUS        = HidNpadButton_Minus,
    KEY_DLEFT        = HidNpadButton_Left,
    KEY_DUP          = HidNpadButton_Up,
    KEY_DRIGHT       = HidNpadButton_Right,
    KEY_DDOWN        = HidNpadButton_Down,
    KEY_LSTICK_LEFT  = HidNpadButton_StickLLeft,
    KEY_LSTICK_UP    = HidNpadButton_StickLUp,
    KEY_LSTICK_RIGHT = HidNpadButton_StickLRight,
    KEY_LSTICK_DOWN  = HidNpadButton_StickLDown,
    KEY_RSTICK_LEFT  = HidNpadButton_StickRLeft,
    KEY_RSTICK_UP    = HidNpadButton_StickRUp,
    KEY_RSTICK_RIGHT = HidNpadButton_StickRRight,
    KEY_RSTICK_DOWN  = HidNpadButton_StickRDown,
    KEY_SL_LEFT      = HidNpadButton_LeftSL,
    KEY_SR_LEFT      = HidNpadButton_LeftSR,
    KEY_SL_RIGHT     = HidNpadButton_RightSL,
    KEY_SR_RIGHT     = HidNpadButton_RightSR,
    KEY_TOUCH        = BITL(28),
    KEY_UP           = HidNpadButton_AnyUp,
    KEY_DOWN         = HidNpadButton_AnyDown,
    KEY_LEFT         = HidNpadButton_AnyLeft,
    KEY_RIGHT        = HidNpadButton_AnyRight,
    KEY_SL           = HidNpadButton_AnySL,
    KEY_SR           = HidNpadButton_AnySR,
} HidControllerKeys;

typedef struct touchPosition {
    u32 px;
    u32 py;
    u32 dx;
    u32 dy;
    u32 angle;
} touchPosition;

typedef struct JoystickPosition {
    s32 dx;
    s32 dy;
} JoystickPosition;

#ifdef __cplusplus
struct NxLegacyHidState {
    bool initialized = false;
    PadState pad{};
    HidTouchScreenState touch{};
    bool prev_touched = false;
    bool cur_touched = false;
};

inline NxLegacyHidState &__nx_legacy_hid_state() {
    static NxLegacyHidState state;
    if (!state.initialized) {
        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        padInitializeAny(&state.pad);
        hidInitializeTouchScreen();
        state.initialized = true;
    }
    return state;
}

inline void hidScanInput(void) {
    auto &st = __nx_legacy_hid_state();
    padUpdate(&st.pad);
    st.prev_touched = st.cur_touched;
    if (hidGetTouchScreenStates(&st.touch, 1) > 0) {
        st.cur_touched = (st.touch.count > 0);
    } else {
        st.touch.count = 0;
        st.cur_touched = false;
    }
}

inline u64 hidKeysDown(HidControllerID id) {
    (void)id;
    auto &st = __nx_legacy_hid_state();
    u64 down = padGetButtonsDown(&st.pad);
    if (st.cur_touched && !st.prev_touched) {
        down |= KEY_TOUCH;
    }
    return down;
}

inline u64 hidKeysHeld(HidControllerID id) {
    (void)id;
    auto &st = __nx_legacy_hid_state();
    u64 held = padGetButtons(&st.pad);
    if (st.cur_touched) {
        held |= KEY_TOUCH;
    }
    return held;
}

inline u64 hidKeysUp(HidControllerID id) {
    (void)id;
    auto &st = __nx_legacy_hid_state();
    u64 up = padGetButtonsUp(&st.pad);
    if (!st.cur_touched && st.prev_touched) {
        up |= KEY_TOUCH;
    }
    return up;
}

inline bool hidIsControllerConnected(HidControllerID id) {
    auto &st = __nx_legacy_hid_state();
    if (id == CONTROLLER_HANDHELD) {
        return padIsHandheld(&st.pad);
    }
    if (id <= CONTROLLER_PLAYER_8) {
        return padIsNpadActive(&st.pad, static_cast<HidNpadIdType>(id));
    }
    return padIsConnected(&st.pad);
}

inline u32 hidTouchCount(void) {
    auto &st = __nx_legacy_hid_state();
    return st.touch.count > 0 ? static_cast<u32>(st.touch.count) : 0;
}

inline void hidTouchRead(touchPosition *pos, u32 point_id) {
    if (!pos) return;
    auto &st = __nx_legacy_hid_state();
    if (static_cast<s32>(point_id) < st.touch.count) {
        pos->px = st.touch.touches[point_id].x;
        pos->py = st.touch.touches[point_id].y;
        pos->dx = st.touch.touches[point_id].diameter_x;
        pos->dy = st.touch.touches[point_id].diameter_y;
        pos->angle = st.touch.touches[point_id].rotation_angle;
    } else {
        pos->px = 0;
        pos->py = 0;
        pos->dx = 0;
        pos->dy = 0;
        pos->angle = 0;
    }
}

inline void hidJoystickRead(JoystickPosition *pos, HidControllerID id, HidControllerJoystick stick) {
    (void)id;
    if (!pos) return;
    auto &st = __nx_legacy_hid_state();
    HidAnalogStickState s = padGetStickPos(&st.pad, static_cast<unsigned>(stick));
    pos->dx = s.x;
    pos->dy = s.y;
}

inline Result plInitialize(void) {
    return plInitialize(PlServiceType_User);
}

inline Result hidsysAcquireHomeButtonEventHandle(Event *hdr) {
    return hidsysAcquireHomeButtonEventHandle(hdr, false);
}

inline Result hidsysAcquireSleepButtonEventHandle(Event *hdr) {
    return hidsysAcquireSleepButtonEventHandle(hdr, false);
}
#endif
