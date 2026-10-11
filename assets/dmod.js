/*
 * dmod in a browser - the page as dmodOS shows it, without dmodOS.
 *
 * On dmodOS the page is a view shown by carhmi: dmod.prop / get / set /
 * onchange / emit are the channel between them (dmview's properties and
 * events), compiled into the view. The conversion does not run this file:
 * here it plays the part of carhmi - the five tracks of the design, a second
 * at a time - so the page works in a browser as it does on the device.
 */
(function () {
    const props = {};
    const listeners = {};

    function changed(name) {
        (listeners[name] || []).forEach(fn => fn());
        /* data-dmod-width="name": as wide as the property says (data-dmod-scale: 1000) */
        document.querySelectorAll('[data-dmod-width="' + name + '"]').forEach(e => {
            const scale = parseInt(e.dataset.dmodScale || '1000', 10);
            e.style.width = Math.max(0, Math.min(scale, props[name] || 0)) * 100 / scale + '%';
        });
    }

    /* What carhmi does: it sets the properties, the view's handlers show them */
    function app(name, value) {
        props[name] = value;
        changed(name);
    }

    function play(track) {
        const n = props['count'];
        track = ((track % n) + n) % n;
        app('track', track);
        app('title', props['row' + track + '.title']);
        app('artist', props['row' + track + '.artist']);
        const t = (props['row' + track + '.time'] || '0:00').split(':');
        app('duration', parseInt(t[0], 10) * 60 + parseInt(t[1], 10));
        app('position', 0);
        app('progress', 0);
        app('playing', 1);
    }

    /* The properties of data-dmod-width elements, from data-dmod-value */
    window.addEventListener('DOMContentLoaded', () => {
        document.querySelectorAll('[data-dmod-width]').forEach(e => {
            app(e.dataset.dmodWidth, parseInt(e.dataset.dmodValue || '0', 10));
        });
    });

    window.dmod = {
        prop(name, initial) {
            if (!(name in props))
                props[name] = initial;
            return props[name];
        },
        get(name) {
            return props[name];
        },
        set(name, value) {
            props[name] = value;
        },
        onchange(name, fn) {
            (listeners[name] = listeners[name] || []).push(fn);
        },
        emit(name, value) {
            if (name === 'toggle')
                app('playing', props['playing'] ? 0 : 1);
            else if (name === 'next')
                play(props['track'] + 1);
            else if (name === 'prev')
                play(props['track'] - 1);
            else if (name === 'select')
                play(value);
        },
    };

    setInterval(() => {
        if (!props['playing'])
            return;
        if (props['position'] + 1 >= props['duration'])
            play(props['track'] + 1);
        else
        {
            app('position', props['position'] + 1);
            app('progress', Math.floor(props['position'] * 1000 / props['duration']));
        }
    }, 1000);
})();
