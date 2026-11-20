import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import St from 'gi://St';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

export default class SurfaceSmartOSKExtension extends Extension {
    enable() {
        this._userDismissed = false;
        this._touchSlots = new Map();
        this._isSliding = false;
        this._slideStartY = 0;
        this._initialTranslationY = 0;

        // 1. Monkey-patch Main.keyboard.open to enforce smart suppression
        this._origManagerOpen = Main.keyboard.open.bind(Main.keyboard);
        Main.keyboard.open = (monitor) => {
            if (this._shouldSuppress()) {
                return;
            }
            return this._origManagerOpen(monitor);
        };

        // 2. Monkey-patch Keyboard actor if it exists or when created
        this._patchKeyboardActor();
        this._visibilityId = Main.keyboard.connect('visibility-changed', () => {
            this._patchKeyboardActor();
        });

        // 3. Reset userDismissed when user explicitly taps on the screen / text field
        this._stageCapturedId = global.stage.connect('captured-event', (actor, event) => {
            const type = event.type();
            if (type === Clutter.EventType.TOUCH_BEGIN || type === Clutter.EventType.BUTTON_PRESS) {
                if (this._userDismissed) {
                    this._userDismissed = false;
                }
            }
            return Clutter.EVENT_PROPAGATE;
        });

        // 4. Reset userDismissed when active window changes
        this._focusWindowId = global.display.connect('notify::focus-window', () => {
            this._userDismissed = false;
        });

        // 5. Connect 2-finger slide down gesture to keyboardBox
        const kbBox = Main.layoutManager.keyboardBox;
        if (kbBox) {
            this._boxCapturedId = kbBox.connect('captured-event', (actor, event) => {
                return this._handleBoxEvent(event);
            });
        }
    }

    disable() {
        if (this._origManagerOpen) {
            Main.keyboard.open = this._origManagerOpen;
            this._origManagerOpen = null;
        }

        if (this._visibilityId) {
            Main.keyboard.disconnect(this._visibilityId);
            this._visibilityId = 0;
        }

        if (this._stageCapturedId) {
            global.stage.disconnect(this._stageCapturedId);
            this._stageCapturedId = 0;
        }

        if (this._focusWindowId) {
            global.display.disconnect(this._focusWindowId);
            this._focusWindowId = 0;
        }

        const kbBox = Main.layoutManager.keyboardBox;
        if (kbBox && this._boxCapturedId) {
            kbBox.disconnect(this._boxCapturedId);
            this._boxCapturedId = 0;
        }

        this._touchSlots.clear();
        this._isSliding = false;
        this._userDismissed = false;
    }

    _patchKeyboardActor() {
        const kb = Main.keyboard._keyboard;
        if (!kb || kb._smartOskPatched)
            return;

        kb._smartOskPatched = true;

        const origActorOpen = kb.open.bind(kb);
        kb.open = (immediate) => {
            if (this._shouldSuppress()) {
                return;
            }
            return origActorOpen(immediate);
        };

        const origActorClose = kb.close.bind(kb);
        kb.close = (immediate) => {
            if (kb.visible || kb._keyboardVisible) {
                this._userDismissed = true;
            }
            return origActorClose(immediate);
        };
    }

    _shouldSuppress() {
        // Suppress if physical keyboard is connected & active
        if (this._isPhysicalKeyboardActive()) {
            return true;
        }

        // Suppress if user explicitly dismissed and hasn't tapped again
        if (this._userDismissed) {
            return true;
        }

        return false;
    }

    _isPhysicalKeyboardActive() {
        const seat = global.stage.context.get_backend().get_default_seat();
        if (seat && seat.get_touch_mode()) {
            // In tablet mode (folded or detached)
            return false;
        }

        try {
            const dir = Gio.File.new_for_path('/sys/class/input');
            if (!dir.query_exists(null))
                return false;

            const enumerator = dir.enumerate_children('standard::name', Gio.FileQueryInfoFlags.NONE, null);
            let info;
            while ((info = enumerator.next_file(null)) !== null) {
                const name = info.get_name();
                if (name.startsWith('input')) {
                    const nameFile = Gio.File.new_for_path(`/sys/class/input/${name}/name`);
                    if (nameFile.query_exists(null)) {
                        const [ok, contents] = nameFile.load_contents(null);
                        if (ok) {
                            const str = new TextDecoder().decode(contents).trim().toLowerCase();
                            if (str.includes('keyboard') && !str.includes('virtual') && !str.includes('ydotool')) {
                                return true;
                            }
                        }
                    }
                }
            }
        } catch (e) {
            // Fallback
        }
        return false;
    }

    _handleBoxEvent(event) {
        const type = event.type();

        // 1. Two-finger scroll / swipe on touchpad over keyboard
        if (type === Clutter.EventType.SCROLL) {
            const dir = event.get_scroll_direction();
            let dy = 0;
            if (dir === Clutter.ScrollDirection.SMOOTH) {
                const [, scrollY] = event.get_scroll_delta();
                dy = scrollY;
            } else if (dir === Clutter.ScrollDirection.DOWN) {
                dy = 1;
            }

            if (dy > 0) {
                this._userDismissed = true;
                Main.keyboard.close();
                return Clutter.EVENT_STOP;
            }
        }

        // 2. Two-finger slide down on touchscreen
        if (type === Clutter.EventType.TOUCH_BEGIN) {
            const seq = event.get_event_sequence();
            const slot = seq ? seq.get_slot() : 0;
            const [x, y] = event.get_coords();
            this._touchSlots.set(slot, { x, y, startY: y });

            if (this._touchSlots.size === 2) {
                this._isSliding = true;
                const pts = Array.from(this._touchSlots.values());
                this._slideStartY = (pts[0].y + pts[1].y) / 2;
                this._initialTranslationY = Main.keyboard._keyboard ? Main.keyboard._keyboard.translation_y : 0;
            }
        } else if (type === Clutter.EventType.TOUCH_UPDATE) {
            const seq = event.get_event_sequence();
            const slot = seq ? seq.get_slot() : 0;
            if (this._touchSlots.has(slot)) {
                const [x, y] = event.get_coords();
                this._touchSlots.get(slot).y = y;
            }

            if (this._isSliding && this._touchSlots.size >= 2 && Main.keyboard._keyboard) {
                const pts = Array.from(this._touchSlots.values());
                const currentAvgY = (pts[0].y + pts[1].y) / 2;
                const deltaY = currentAvgY - this._slideStartY;

                // Dragging down
                if (deltaY > 0) {
                    Main.keyboard._keyboard.translation_y = this._initialTranslationY + deltaY;
                    return Clutter.EVENT_STOP; // Avoid accidental key presses while sliding
                }
            }
        } else if (type === Clutter.EventType.TOUCH_END || type === Clutter.EventType.TOUCH_CANCEL) {
            const seq = event.get_event_sequence();
            const slot = seq ? seq.get_slot() : 0;
            this._touchSlots.delete(slot);

            if (this._isSliding && this._touchSlots.size < 2) {
                this._isSliding = false;
                if (Main.keyboard._keyboard) {
                    const currentY = Main.keyboard._keyboard.translation_y;
                    const deltaY = currentY - this._initialTranslationY;

                    if (deltaY > 40) {
                        // Dismiss!
                        this._userDismissed = true;
                        Main.keyboard.close();
                    } else {
                        // Snap back
                        Main.keyboard._keyboard.ease({
                            translation_y: this._initialTranslationY,
                            duration: 150,
                            mode: Clutter.AnimationMode.EASE_OUT_QUAD,
                        });
                    }
                }
            }
        }

        return Clutter.EVENT_PROPAGATE;
    }
}
