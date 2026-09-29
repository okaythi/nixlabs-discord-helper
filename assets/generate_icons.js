#!/usr/bin/env gjs
const { GdkPixbuf, Gio, GLib } = imports.gi;

const svgPath = GLib.build_filenamev([GLib.get_current_dir(), 'assets', 'nixlabs-discord-helper.svg']);
const sizes = [16, 24, 32, 48, 64, 128, 256, 512];

for (let size of sizes) {
    let outDir = GLib.build_filenamev([GLib.get_current_dir(), 'assets', 'icons', `${size}x${size}`]);
    GLib.mkdir_with_parents(outDir, 0o755);
    let outFile = GLib.build_filenamev([outDir, 'nixlabs-discord-helper.png']);
    
    let pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_scale(svgPath, size, size, true);
    pixbuf.savev(outFile, 'png', [], []);
    print(`Generated pristine ${size}x${size} icon at ${outFile}`);
}
