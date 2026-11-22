import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GioUnix from 'gi://GioUnix';
import GLib from 'gi://GLib';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';

const EV_SYN = 0;
const EV_KEY = 1;
const EV_ABS = 3;
const SYN_REPORT = 0;
const ABS_X = 0;
const ABS_Y = 1;
const BTN_TOOL_PEN = 320;
const BTN_TOOL_RUBBER = 321;
const BTN_TOUCH = 330;

// Verified IPTSD Virtual Stylus 045E:0C21 ranges
const MAX_X = 9600.0;
const MAX_Y = 7200.0;
const EVENT_SIZE = 24; // aarch64 struct input_event (8+8+2+2+4)

const HOVER_SIZE = 10;
const PRESS_SIZE = 8;
const ERASER_SIZE = 14;
const HIDE_TIMEOUT_MS = 450;

export class SurfacePenDotWorker {
    enable() {
        try {
            const ext = Main.extensionManager?.lookup?.('surface-pen-dot@linux-surface');
            if (ext?.stateObj && ext.stateObj !== this && ext.stateObj._dot) {
                ext.stateObj._destroyed = true;
                ext.stateObj._clearHideTimer?.();
                ext.stateObj._clearRetryTimer?.();
                ext.stateObj._closeStylus?.();
                ext.stateObj._dot.destroy();
                ext.stateObj._dot = null;
            }
        } catch (_e) {}

        this._destroyed = false;
        this._inRange = false;
        this._pressed = false;
        this._isEraser = false;
        this._curX = 0;
        this._curY = 0;
        this._hasPos = false;
        this._curSize = 0;
        this._transform = 0;
        this._cursorInhibited = false;
        this._noneCursor = null;

        this._hideSourceId = 0;
        this._retrySourceId = 0;
        this._idleHideCursorId = 0;
        this._source = null;
        this._channel = null;
        this._stream = null;

        this._dot = new St.Widget({
            reactive: false,
            can_focus: false,
            visible: false,
        });
        this._applyStyle(HOVER_SIZE, false);
        Main.uiGroup.add_child(this._dot);

        this._monitorsChangedId = Main.layoutManager.connect('monitors-changed', () => {
            this._refreshTransform();
        });
        this._refreshTransform();
        this._openStylus();
    }

    disable() {
        this._destroyed = true;
        if (this._monitorsChangedId) {
            Main.layoutManager.disconnect(this._monitorsChangedId);
            this._monitorsChangedId = 0;
        }
        this._clearHideTimer();
        this._clearRetryTimer();
        this._clearIdleCursorTimer();
        this._closeStylus();
        this._restoreCursorVisibility();
        if (this._dot) {
            this._dot.destroy();
            this._dot = null;
        }
    }

    _findStylusPath() {
        try {
            const dir = Gio.File.new_for_path('/sys/class/input');
            const en = dir.enumerate_children('standard::name', Gio.FileQueryInfoFlags.NONE, null);
            let info;
            while ((info = en.next_file(null)) !== null) {
                const name = info.get_name();
                if (!name.startsWith('event'))
                    continue;
                const f = Gio.File.new_for_path(`/sys/class/input/${name}/device/name`);
                if (!f.query_exists(null))
                    continue;
                try {
                    const [ok, bytes] = f.load_contents(null);
                    if (ok) {
                        const devName = new TextDecoder().decode(bytes).trim();
                        if (devName.startsWith('IPTSD Virtual Stylus')) {
                            en.close(null);
                            return `/dev/input/${name}`;
                        }
                    }
                } catch (_e) {
                    // ignore unreadable nodes
                }
            }
            en.close(null);
        } catch (_e) {
            // ignore
        }
        return null;
    }

    _openStylus() {
        if (this._destroyed)
            return;
        this._closeStylus();

        const path = this._findStylusPath();
        if (!path) {
            this._scheduleRetry();
            return;
        }

        try {
            const ch = GLib.IOChannel.new_file(path, 'r');
            ch.set_encoding(null);
            ch.set_buffered(false);
            ch.set_flags(GLib.IOFlags.NONBLOCK);
            const fd = ch.unix_get_fd();

            this._channel = ch;
            this._stream = new GioUnix.InputStream({ fd, close_fd: false });
            this._source = this._stream.create_source(null);
            this._source.set_priority(GLib.PRIORITY_HIGH);
            this._source.set_callback(() => {
                if (this._destroyed || !this._stream)
                    return GLib.SOURCE_REMOVE;
                try {
                    if (!this._stream.is_readable())
                        return GLib.SOURCE_CONTINUE;
                    const bytes = this._stream.read_bytes(EVENT_SIZE * 64, null);
                    const data = bytes ? bytes.get_data() : null;
                    if (!data || data.byteLength === 0) {
                        this._hideDot();
                        this._closeStylus();
                        this._scheduleRetry();
                        return GLib.SOURCE_REMOVE;
                    }
                    this._processBuffer(data);
                    return GLib.SOURCE_CONTINUE;
                } catch (e) {
                    if (e.matches && e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.WOULD_BLOCK))
                        return GLib.SOURCE_CONTINUE;
                    this._hideDot();
                    this._closeStylus();
                    this._scheduleRetry();
                    return GLib.SOURCE_REMOVE;
                }
            });
            this._source.attach(null);
            console.log('[SURFACE_PEN_DOT] Active on', path, 'fd=', fd);
        } catch (e) {
            console.error('[SURFACE_PEN_DOT] Open failed:', e);
            this._closeStylus();
            this._scheduleRetry();
        }
    }

    _closeStylus() {
        if (this._source) {
            try {
                this._source.destroy();
            } catch (_e) {}
            this._source = null;
        }
        if (this._stream) {
            try {
                this._stream.close(null);
            } catch (_e) {}
            this._stream = null;
        }
        if (this._channel) {
            try {
                this._channel.shutdown(false);
            } catch (_e) {}
            this._channel = null;
        }
    }

    _scheduleRetry() {
        this._clearRetryTimer();
        if (this._destroyed)
            return;
        this._retrySourceId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 3000, () => {
            this._retrySourceId = 0;
            this._openStylus();
            return GLib.SOURCE_REMOVE;
        });
    }

    _clearRetryTimer() {
        if (this._retrySourceId) {
            GLib.source_remove(this._retrySourceId);
            this._retrySourceId = 0;
        }
    }

    _clearIdleCursorTimer() {
        if (this._idleHideCursorId) {
            GLib.source_remove(this._idleHideCursorId);
            this._idleHideCursorId = 0;
        }
    }

    _processBuffer(data) {
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        const count = Math.floor(data.byteLength / EVENT_SIZE);
        let dirty = false;

        for (let i = 0; i < count; i++) {
            const off = i * EVENT_SIZE;
            const type = view.getUint16(off + 16, true);
            const code = view.getUint16(off + 18, true);
            const value = view.getInt32(off + 20, true);

            if (type === EV_ABS) {
                if (code === ABS_X) {
                    this._curX = value;
                    this._hasPos = true;
                } else if (code === ABS_Y) {
                    this._curY = value;
                    this._hasPos = true;
                }
            } else if (type === EV_KEY) {
                if (code === BTN_TOOL_PEN) {
                    if (value === 1) {
                        this._inRange = true;
                        this._isEraser = false;
                    } else if (!this._isEraser) {
                        this._inRange = false;
                        this._pressed = false;
                    }
                } else if (code === BTN_TOOL_RUBBER) {
                    if (value === 1) {
                        this._inRange = true;
                        this._isEraser = true;
                    } else if (this._isEraser) {
                        this._inRange = false;
                        this._pressed = false;
                        this._isEraser = false;
                    }
                } else if (code === BTN_TOUCH) {
                    this._pressed = (value === 1);
                    if (this._pressed)
                        this._inRange = true;
                }
            } else if (type === EV_SYN && code === SYN_REPORT) {
                dirty = true;
            }
        }

        if (dirty)
            this._updateDot();
    }

    _inhibitCursorVisibility() {
        if (!this._cursorInhibited) {
            this._cursorInhibited = true;
            try {
                const tracker = global.backend.get_cursor_tracker();
                if (tracker && tracker.inhibit_cursor_visibility)
                    tracker.inhibit_cursor_visibility();
            } catch (_e) {}
            try {
                if (global.stage && global.stage.set_cursor_type)
                    global.stage.set_cursor_type(Clutter.CursorType.NONE);
            } catch (_e) {}
        }
    }

    _restoreCursorVisibility() {
        this._clearIdleCursorTimer();
        if (this._cursorInhibited) {
            this._cursorInhibited = false;
            try {
                if (global.stage && global.stage.set_cursor_type)
                    global.stage.set_cursor_type(Clutter.CursorType.INHERIT);
            } catch (_e) {}
            try {
                const tracker = global.backend.get_cursor_tracker();
                if (tracker && tracker.uninhibit_cursor_visibility)
                    tracker.uninhibit_cursor_visibility();
            } catch (_e) {}
        }
    }

    _updateDot() {
        if (!this._dot)
            return;
        if (!this._inRange) {
            this._hideDot();
            return;
        }
        if (!this._hasPos)
            return;

        const mon = Main.layoutManager.primaryMonitor;
        if (!mon)
            return;

        const ax = Math.max(0.0, Math.min(1.0, this._curX / MAX_X));
        const ay = Math.max(0.0, Math.min(1.0, this._curY / MAX_Y));
        let nx = ax;
        let ny = ay;
        switch (this._transform) {
            case 1: // 90 deg CCW
                nx = ay;
                ny = 1.0 - ax;
                break;
            case 2: // 180 deg
                nx = 1.0 - ax;
                ny = 1.0 - ay;
                break;
            case 3: // 270 deg CCW (90 deg CW)
                nx = 1.0 - ay;
                ny = ax;
                break;
            default:
                break;
        }

        const px = mon.x + nx * mon.width;
        const py = mon.y + ny * mon.height;
        const size = this._isEraser ? ERASER_SIZE : (this._pressed ? PRESS_SIZE : HOVER_SIZE);
        if (this._curSize !== size || this._styledEraser !== this._isEraser)
            this._applyStyle(size, this._isEraser);

        this._dot.set_position(Math.round(px - size / 2), Math.round(py - size / 2));
        if (!this._dot.visible) {
            Main.uiGroup.set_child_above_sibling(this._dot, null);
            this._dot.show();
        }
        this._inhibitCursorVisibility();
        this._resetHideTimer();
    }

    _refreshTransform() {
        Gio.DBus.session.call(
            'org.gnome.Mutter.DisplayConfig',
            '/org/gnome/Mutter/DisplayConfig',
            'org.gnome.Mutter.DisplayConfig',
            'GetCurrentState',
            null,
            null,
            Gio.DBusCallFlags.NONE,
            1000,
            null,
            (conn, res) => {
                if (this._destroyed)
                    return;
                try {
                    const reply = conn.call_finish(res);
                    const [, , logicalMonitors] = reply.deepUnpack();
                    for (const lm of logicalMonitors) {
                        if (lm[4] === true) {
                            this._transform = lm[3] & 3;
                            return;
                        }
                    }
                    if (logicalMonitors.length > 0)
                        this._transform = logicalMonitors[0][3] & 3;
                } catch (_e) {
                    // keep previous transform
                }
            }
        );
    }

    _applyStyle(size, isEraser) {
        this._curSize = size;
        this._styledEraser = isEraser;
        this._dot.set_size(size, size);
        if (isEraser) {
            this._dot.set_style(
                'background-color: rgba(255, 255, 255, 0.25);' +
                'border: 2px solid rgba(255, 255, 255, 0.95);' +
                'border-radius: 99px;' +
                'box-shadow: 0 0 4px rgba(0, 0, 0, 0.65);'
            );
        } else {
            this._dot.set_style(
                'background-color: rgba(255, 255, 255, 0.95);' +
                'border: 1.5px solid rgba(0, 0, 0, 0.70);' +
                'border-radius: 99px;' +
                'box-shadow: 0 0 4px rgba(0, 0, 0, 0.45);'
            );
        }
    }

    _resetHideTimer() {
        this._clearHideTimer();
        this._hideSourceId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, HIDE_TIMEOUT_MS, () => {
            this._hideSourceId = 0;
            this._inRange = false;
            this._pressed = false;
            this._hideDot();
            return GLib.SOURCE_REMOVE;
        });
    }

    _clearHideTimer() {
        if (this._hideSourceId) {
            GLib.source_remove(this._hideSourceId);
            this._hideSourceId = 0;
        }
    }

    _hideDot() {
        this._clearHideTimer();
        if (this._dot && this._dot.visible)
            this._dot.hide();
        this._restoreCursorVisibility();
    }
}
