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
        const n = props['total'] || props['count'];
        track = ((track % n) + n) % n;
        
        // Oblicz which page/buffer the track is in
        const first = props['first'] || 0;
        const ROWS = 10;
        
        // Sprawdź czy utwór jest w aktualnie załadowanym buforze
        if (track < first || track >= first + ROWS) {
            // Załaduj nową stronę
            const newFirst = Math.floor(track / ROWS) * ROWS;
            loadRows(newFirst);
        }
        
        // Pobierz dane utworu (z aktualnie załadowanego bufora)
        const relativeIndex = track - (props['first'] || 0);
        app('track', track);
        app('title', props['row' + relativeIndex + '.title'] || '');
        app('artist', props['row' + relativeIndex + '.artist'] || '');
        const t = (props['row' + relativeIndex + '.time'] || '0:00').split(':');
        app('duration', parseInt(t[0], 10) * 60 + parseInt(t[1], 10));
        app('position', 0);
        app('progress', 0);
        app('playing', 1);
    }

    function loadRows(first) {
        const ROWS = 10;
        props['first'] = first;
        // Demo mode: generate track data based on index
        for (let i = 0; i < ROWS; i++) {
            const idx = first + i;
            // W trybie demo, generujemy dane na podstawie indeksu
            // W prawdziwym urządzeniu carhmi wysyła te dane
            if (idx < (props['total'] || 25)) {
                // Demo tracks - in real device this comes from carhmi
                const titles = ['Neonowe Noce', 'Cyberpunk City', 'Chill Vibes', 'Midnight Drive', 'Retro Wave',
                    'Electric Dreams', 'Neon Rain', 'Starlight', 'Pulse', 'Horizon',
                    'Velocity', 'Echoes', 'Momentum', 'Synth City', 'Orbit',
                    'Voltage', 'Spectrum', 'Phantom', 'Titan', 'Solar',
                    'Lunar', 'Nova', 'Quantum', 'Signal', 'Matrix'];
                const artists = ['Synthwave Rider', 'Neo Tokyo', 'Lofi Beats', 'Nightcall', '80s Synth',
                    'Digital Frontier', 'Cyber Minds', 'Astro Beats', 'Techno Core', 'Ambient Sky',
                    'Speed Demons', 'Space Voyager', 'Fast Lane', 'Digital Dawn', 'Cosmic Sound',
                    'Electro Rush', 'Rainbow Beats', 'Dark Waves', 'Heavy Metal Electronic', 'Sun Ray',
                    'Moon Sound', 'Star Burst', 'Future Tech', 'Radio Wave', 'Digital Rain'];
                const times = [225, 252, 170, 305, 210, 198, 267, 234, 189, 312, 201, 278, 215, 243, 289, 176, 298, 321, 245, 267, 234, 198, 312, 221, 289];
                app('row' + i + '.title', titles[idx] || '');
                app('row' + i + '.artist', artists[idx] || '');
                app('row' + i + '.time', formatTime(times[idx]));
            } else {
                app('row' + i + '.title', '');
                app('row' + i + '.artist', '');
                app('row' + i + '.time', '');
            }
        }
        changed('first');
    }
    
    function formatTime(sec) {
        const m = Math.floor(sec / 60);
        const s = Math.floor(sec % 60);
        return `${m}:${s.toString().padStart(2, '0')}`;
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
            else if (name === 'scroll')
            {
                // Obsługa przewijania wirtualnej listy
                const delta = value;
                const total = props['total'] || props['count'];
                const first = props['first'] || 0;
                const ROWS = 10;
                let newFirst = first + delta;
                if (newFirst < 0) newFirst = 0;
                if (newFirst + ROWS > total) newFirst = (total > ROWS) ? total - ROWS : 0;
                loadRows(newFirst);
            }
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
