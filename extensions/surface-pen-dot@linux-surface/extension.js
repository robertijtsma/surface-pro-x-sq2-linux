import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

export default class SurfacePenDotExtension extends Extension {
    enable() {
        this._stopped = false;
        import(`./dotWorker.js?t=${Date.now()}`).then(mod => {
            if (this._stopped)
                return;
            if (global._surfacePenDotWorker) {
                try {
                    global._surfacePenDotWorker.disable();
                } catch (_e) {}
                global._surfacePenDotWorker = null;
            }
            this._worker = new mod.SurfacePenDotWorker();
            global._surfacePenDotWorker = this._worker;
            this._worker.enable();
        }).catch(console.error);
    }

    disable() {
        this._stopped = true;
        if (this._worker) {
            try {
                this._worker.disable();
            } catch (_e) {}
            if (global._surfacePenDotWorker === this._worker)
                global._surfacePenDotWorker = null;
            this._worker = null;
        }
    }
}
