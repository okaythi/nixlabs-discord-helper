#!/usr/bin/env gjs
imports.gi.versions.Gtk = '3.0';
imports.gi.versions.WebKit2 = '4.1';

const { Gtk, Gdk, GdkPixbuf, WebKit2, GLib, Gio } = imports.gi;

// Give the GJS-hosted app its own identity. Desktop shells otherwise attribute
// an unresponsive window to the generic "gjs" runtime.
GLib.set_prgname('nixlabs-discord-helper');
GLib.set_application_name('Nixlabs DiscordHelper');
Gdk.set_program_class('Nixlabs DiscordHelper');

Gtk.init(null);

const PORT = 45731;
const APP_URL = `http://127.0.0.1:${PORT}`;

// Apply dark CSS background to GTK window, container, and viewport to eliminate white flash
const cssProvider = new Gtk.CssProvider();
cssProvider.load_from_data(`
    window, decoration, .background, viewport {
        background-color: #1e1f20;
    }
`);
Gtk.StyleContext.add_provider_for_screen(
    Gdk.Screen.get_default(),
    cssProvider,
    Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
);

// Find binary location
let binPath = '/usr/lib/nixlabs-discord-helper/nixlabs-discord-helper-bin';
if (!GLib.file_test(binPath, GLib.FileTest.EXISTS)) {
    let currentDir = GLib.get_current_dir();
    let devBin = GLib.build_filenamev([currentDir, 'backend', 'nixlabs-discord-helper-bin']);
    if (GLib.file_test(devBin, GLib.FileTest.EXISTS)) {
        binPath = devBin;
    }
}

// Spawn backend server if not running
let backendProc = null;
try {
    let [res, pid] = GLib.spawn_async(
        null,
        [binPath, '-port', PORT.toString()],
        null,
        GLib.SpawnFlags.SEARCH_PATH | GLib.SpawnFlags.DO_NOT_REAP_CHILD,
        null
    );
    if (res) {
        backendProc = pid;
    }
} catch (e) {
    // Backend already running or spawn note
}

// Create native desktop window
Gtk.Window.set_default_icon_name('nixlabs-discord-helper');

const win = new Gtk.Window({
    title: 'Discord Helper',
    window_position: Gtk.WindowPosition.CENTER,
    default_width: 1024,
    default_height: 720
});

// Keep the window associated with the desktop entry in docks, app switchers,
// and compositor "not responding" dialogs.
win.set_wmclass('nixlabs-discord-helper', 'Nixlabs DiscordHelper');

// Set multi-resolution icon list for pristine quality across taskbar, tray, alt-tab, and dock
const iconSizes = [16, 24, 32, 48, 64, 128, 256, 512];
let iconList = [];
for (let s of iconSizes) {
    let p = `/usr/share/icons/hicolor/${s}x${s}/apps/nixlabs-discord-helper.png`;
    if (!GLib.file_test(p, GLib.FileTest.EXISTS)) {
        let currentDir = GLib.get_current_dir();
        p = GLib.build_filenamev([currentDir, 'assets', 'icons', `${s}x${s}`, 'nixlabs-discord-helper.png']);
    }
    if (GLib.file_test(p, GLib.FileTest.EXISTS)) {
        try {
            iconList.push(GdkPixbuf.Pixbuf.new_from_file(p));
        } catch (e) {}
    }
}

if (iconList.length > 0) {
    win.set_icon_list(iconList);
} else {
    const svgPath = '/usr/share/icons/hicolor/scalable/apps/nixlabs-discord-helper.svg';
    if (GLib.file_test(svgPath, GLib.FileTest.EXISTS)) {
        try {
            win.set_icon_from_file(svgPath);
        } catch (e) {}
    }
}

// Resizability settings: allow fluid resizing down to 480x400
const geom = new Gdk.Geometry();
geom.min_width = 480;
geom.min_height = 400;
win.set_geometry_hints(null, geom, Gdk.WindowHints.MIN_SIZE);

// Enable discrete and smooth scroll event handling on window and webview
win.add_events(Gdk.EventMask.SCROLL_MASK | Gdk.EventMask.SMOOTH_SCROLL_MASK | Gdk.EventMask.KEY_PRESS_MASK);

const webview = new WebKit2.WebView();
webview.add_events(Gdk.EventMask.SCROLL_MASK | Gdk.EventMask.SMOOTH_SCROLL_MASK);

// Set dark background color on WebKit WebView so fast kinetic scrolling never exposes white tiles
const bgRGBA = new Gdk.RGBA();
bgRGBA.parse('#1e1f20');
webview.set_background_color(bgRGBA);

// Modern web settings
const settings = webview.get_settings();
settings.enable_developer_extras = false;
settings.javascript_can_open_windows_automatically = true;

function openExternal(uri) {
    if (!uri) return;
    try {
        Gio.AppInfo.launch_default_for_uri(uri, null);
    } catch (err) {
        try {
            GLib.spawn_command_line_async(`xdg-open ${GLib.shell_quote(uri)}`);
        } catch (e) {}
    }
}

// ── Strict Zoom Prevention: Lock zoom level to 1.0 at all times ──
webview.connect('notify::zoom-level', () => {
    if (webview.get_zoom_level() !== 1.0) {
        webview.set_zoom_level(1.0);
    }
});

// Intercept scroll events: eat Ctrl + scroll so WebKit cannot trigger zoom, allow normal scroll
webview.connect('scroll-event', (v, event) => {
    let [, state] = event.get_state();
    if ((state & Gdk.ModifierType.CONTROL_MASK) !== 0) {
        return true; // Eat Ctrl + scroll
    }
    return false; // Propagate normal mouse wheel / trackpad scroll
});

// Intercept key events: block Ctrl + (+, -, =, _, 0, Keypad) zoom accelerators
win.connect('key-press-event', (w, event) => {
    let [, state] = event.get_state();
    let [, keyval] = event.get_keyval();
    let isCtrl = (state & Gdk.ModifierType.CONTROL_MASK) !== 0;
    if (isCtrl) {
        if (
            keyval === Gdk.KEY_plus ||
            keyval === Gdk.KEY_minus ||
            keyval === Gdk.KEY_equal ||
            keyval === Gdk.KEY_underscore ||
            keyval === Gdk.KEY_0 ||
            keyval === Gdk.KEY_KP_Add ||
            keyval === Gdk.KEY_KP_Subtract ||
            keyval === Gdk.KEY_KP_0
        ) {
            return true; // Eat zoom keystroke
        }
    }
    return false;
});

// Intercept window.open() calls (e.g. from browser authentication / create account)
webview.connect('create', (view, navAction) => {
    let req = navAction.get_request();
    let uri = req ? req.get_uri() : null;
    if (uri && (uri.startsWith('http://') || uri.startsWith('https://'))) {
        openExternal(uri);
    }
    return null;
});

// Ensure links target external browser when requested
webview.connect('decide-policy', (view, decision, type) => {
    if (type === WebKit2.PolicyDecisionType.NAVIGATION_ACTION ||
        type === WebKit2.PolicyDecisionType.NEW_WINDOW_ACTION) {
        let navAction = decision.get_navigation_action();
        let uri = navAction.get_request().get_uri();
        if (uri.startsWith('https://accounts.nixlabs.tech') ||
            uri.startsWith('https://nixlabs.tech') ||
            (uri.startsWith('http') && !uri.startsWith(APP_URL))) {
            decision.ignore();
            openExternal(uri);
            return true;
        }
    }
    return false;
});

// Handle window close
win.connect('destroy', () => {
    if (backendProc) {
        try {
            GLib.spawn_command_line_sync(`kill -TERM ${backendProc}`);
        } catch (e) {}
    }
    Gtk.main_quit();
});

win.add(webview);
win.show_all();

// Give backend brief time to bind, then load
GLib.timeout_add(GLib.PRIORITY_DEFAULT, 300, () => {
    webview.load_uri(APP_URL);
    return GLib.SOURCE_REMOVE;
});

Gtk.main();
