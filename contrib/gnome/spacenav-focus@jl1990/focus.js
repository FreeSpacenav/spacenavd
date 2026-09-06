// SPDX-License-Identifier: GPL-3.0-or-later
export function focusedAppId(window, tracker, unavailable) {
    if (unavailable || !window)
        return '';
    const id = tracker.get_window_app(window)?.get_id() || window.get_wm_class() || '';
    if (new TextEncoder().encode(id).length > 255 || /[\x00-\x1f\x7f]/.test(id))
        return '';
    return id;
}
