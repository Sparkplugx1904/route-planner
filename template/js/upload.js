/* upload.js — upload CSV (drag & drop + tombol) → parse via WASM */
'use strict';

const Uploader = {
    init(fileInput, statusEl, onParsed) {
        fileInput.addEventListener('change', () => {
            if (fileInput.files.length) Uploader.handleFile(fileInput.files[0], onParsed);
        });

        const dropTarget = document.getElementById('drop-zone');
        if (dropTarget) {
            ['dragenter', 'dragover'].forEach(ev => dropTarget.addEventListener(ev, e => {
                e.preventDefault(); dropTarget.classList.add('dragover');
            }));
            ['dragleave', 'drop'].forEach(ev => dropTarget.addEventListener(ev, e => {
                e.preventDefault(); dropTarget.classList.remove('dragover');
            }));
            dropTarget.addEventListener('drop', e => {
                const f = e.dataTransfer.files[0];
                if (f) Uploader.handleFile(f, onParsed);
            });
        }
    },

    handleFile(file, onParsed) {
        const reader = new FileReader();
        reader.onload = async () => {
            const csv = String(reader.result);
            const status = document.getElementById('csv-status');
            status.textContent = tl('works');
            try {
                const locations = await WorkerBridge.request('parseCsv', { csv });
                if (locations.error) throw new Error(locations.error);
                if (onParsed) onParsed(locations);
            } catch (e) {
                status.textContent = tl('invalid_csv', { msg: e.message });
            }
        };
        reader.readAsText(file, 'utf-8');
    }
};