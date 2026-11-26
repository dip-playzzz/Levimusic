package com.levimusic.picker;

import android.app.Activity;
import android.app.Application;
import android.app.Fragment;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.WeakReference;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Locale;
import java.util.Map;

/**
 * Opens Android's system file picker (Google Files / Documents UI) from the
 * native LeviMusic mod and copies the chosen songs into the mod's music folder.
 *
 * This class is compiled to a tiny dex at build time, embedded in the native
 * library and loaded at runtime with InMemoryDexClassLoader. The native side only
 * calls the static methods below and polls the counters.
 */
public final class Bridge {
    public static final int IDLE = 0;
    public static final int PICKING = 1;
    public static final int COPYING = 2;
    public static final int FINISHED = 3;
    public static final int ERROR = 4;

    private static final int REQUEST_CODE = 0x4C4D;
    private static final String[] AUDIO_EXTENSIONS = {
        ".mp3", ".m4a", ".aac", ".ogg", ".oga", ".opus", ".wav", ".flac",
        ".wma", ".amr", ".mid", ".midi", ".mka", ".aif", ".aiff", ".ac3"
    };

    private static volatile int sState = IDLE;
    private static volatile int sDone;
    private static volatile int sTotal;
    private static volatile int sFailed;
    private static volatile boolean sFinishedFlag;
    private static volatile String sDestDir;
    private static volatile WeakReference<Activity> sCurrent;
    private static boolean sInstalled;

    private Bridge() {}

    /** Called once by native code; starts tracking the foreground activity. */
    public static synchronized void install(Application app, String destDir) {
        sDestDir = destDir;
        if (sInstalled) return;
        sInstalled = true;
        app.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityCreated(Activity a, Bundle b) { sCurrent = new WeakReference<>(a); }
            @Override public void onActivityStarted(Activity a) { sCurrent = new WeakReference<>(a); }
            @Override public void onActivityResumed(Activity a) { sCurrent = new WeakReference<>(a); }
            @Override public void onActivityPaused(Activity a) {}
            @Override public void onActivityStopped(Activity a) {}
            @Override public void onActivitySaveInstanceState(Activity a, Bundle b) {}
            @Override public void onActivityDestroyed(Activity a) {
                Activity c = sCurrent != null ? sCurrent.get() : null;
                if (c == a) sCurrent = null;
            }
        });
    }

    public static int state() { return sState; }
    public static int done() { return sDone; }
    public static int total() { return sTotal; }
    public static int failed() { return sFailed; }

    /** True exactly once after an import finished (so the library can rescan). */
    public static synchronized boolean takeFinished() {
        final boolean value = sFinishedFlag;
        sFinishedFlag = false;
        return value;
    }

    private static Activity findActivity() {
        Activity a = sCurrent != null ? sCurrent.get() : null;
        if (a != null && !a.isFinishing() && !a.isDestroyed()) return a;
        a = null;
        try {
            Class<?> thread = Class.forName("android.app.ActivityThread");
            Object current = thread.getMethod("currentActivityThread").invoke(null);
            Field field = thread.getDeclaredField("mActivities");
            field.setAccessible(true);
            Map<?, ?> records = (Map<?, ?>) field.get(current);
            for (Object record : records.values()) {
                Field activityField = record.getClass().getDeclaredField("activity");
                activityField.setAccessible(true);
                Activity candidate = (Activity) activityField.get(record);
                if (candidate != null && !candidate.isFinishing() && !candidate.isDestroyed()) a = candidate;
            }
        } catch (Throwable ignored) {
            // Falls through: no activity found.
        }
        return a;
    }

    /** Opens the system picker. Safe to call from any thread. */
    public static void open() {
        if (sState == PICKING || sState == COPYING) return;
        final Activity activity = findActivity();
        if (activity == null) {
            sState = ERROR;
            return;
        }
        sState = PICKING;
        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    activity.getFragmentManager().beginTransaction()
                        .add(new Host(), "levimusic_picker").commitAllowingStateLoss();
                } catch (Throwable t) {
                    sState = ERROR;
                }
            }
        });
    }

    /** Headless fragment: it exists only to receive onActivityResult. */
    public static class Host extends Fragment {
        private boolean launched;

        @Override public void onActivityCreated(Bundle savedInstanceState) {
            super.onActivityCreated(savedInstanceState);
            if (launched) return;
            launched = true;
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("audio/*");
                intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                startActivityForResult(intent, REQUEST_CODE);
            } catch (Throwable t) {
                sState = ERROR;
                detach();
            }
        }

        private void detach() {
            try {
                getFragmentManager().beginTransaction().remove(this).commitAllowingStateLoss();
            } catch (Throwable ignored) {
            }
        }

        @Override public void onActivityResult(int requestCode, int resultCode, Intent data) {
            if (requestCode != REQUEST_CODE) return;
            final Context context = getActivity() != null ? getActivity().getApplicationContext() : null;
            detach();
            if (context == null || resultCode != Activity.RESULT_OK || data == null) {
                sState = IDLE;
                return;
            }
            final ArrayList<Uri> uris = new ArrayList<>();
            ClipData clip = data.getClipData();
            if (clip != null) {
                for (int i = 0; i < clip.getItemCount(); i++) {
                    Uri uri = clip.getItemAt(i).getUri();
                    if (uri != null) uris.add(uri);
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
            if (uris.isEmpty()) {
                sState = IDLE;
                return;
            }
            sDone = 0;
            sFailed = 0;
            sTotal = uris.size();
            sState = COPYING;
            new Thread(new Runnable() {
                @Override public void run() { copyAll(context, uris); }
            }, "LeviMusic-import").start();
        }
    }

    private static void copyAll(Context context, ArrayList<Uri> uris) {
        try {
            final File dir = new File(sDestDir);
            //noinspection ResultOfMethodCallIgnored
            dir.mkdirs();
            final ContentResolver resolver = context.getContentResolver();
            for (Uri uri : uris) {
                try {
                    copyOne(resolver, dir, uri);
                } catch (Throwable t) {
                    sFailed++;
                }
                sDone++;
            }
        } finally {
            synchronized (Bridge.class) {
                sState = FINISHED;
                sFinishedFlag = true;
            }
        }
    }

    private static void copyOne(ContentResolver resolver, File dir, Uri uri) throws Exception {
        String name = null;
        Cursor cursor = null;
        try {
            cursor = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null);
            if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
        } catch (Throwable ignored) {
        } finally {
            if (cursor != null) cursor.close();
        }
        if (name == null || name.isEmpty()) name = uri.getLastPathSegment();
        if (name == null || name.isEmpty()) name = "song";
        name = name.replaceAll("[\\\\/:*?\"<>|]", "_");
        while (name.startsWith(".")) name = name.substring(1);
        if (name.isEmpty()) name = "song";

        if (!hasAudioExtension(name)) {
            final String ext = extensionForMime(resolver.getType(uri));
            if (ext == null) throw new IllegalStateException("not an audio file");
            name = name + ext;
        }

        File target = new File(dir, name);
        if (target.exists()) {
            final int dot = name.lastIndexOf('.');
            final String stem = dot > 0 ? name.substring(0, dot) : name;
            final String ext = dot > 0 ? name.substring(dot) : "";
            for (int n = 2; n < 1000 && target.exists(); n++) {
                target = new File(dir, stem + " (" + n + ")" + ext);
            }
        }
        final File part = new File(dir, target.getName() + ".part");

        InputStream in = null;
        OutputStream out = null;
        boolean ok = false;
        try {
            in = resolver.openInputStream(uri);
            if (in == null) throw new IllegalStateException("cannot open");
            out = new java.io.FileOutputStream(part);
            final byte[] buffer = new byte[256 * 1024];
            int read;
            while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
            out.flush();
            ok = true;
        } finally {
            try { if (in != null) in.close(); } catch (Throwable ignored) {}
            try { if (out != null) out.close(); } catch (Throwable ignored) {}
            if (!ok) //noinspection ResultOfMethodCallIgnored
                part.delete();
        }
        if (!part.renameTo(target)) {
            //noinspection ResultOfMethodCallIgnored
            part.delete();
            throw new IllegalStateException("rename failed");
        }
    }

    private static boolean hasAudioExtension(String name) {
        final String lower = name.toLowerCase(Locale.ROOT);
        for (String ext : AUDIO_EXTENSIONS) {
            if (lower.endsWith(ext)) return true;
        }
        return false;
    }

    private static String extensionForMime(String mime) {
        if (mime == null) return null;
        switch (mime.toLowerCase(Locale.ROOT)) {
            case "audio/mpeg": case "audio/mp3": return ".mp3";
            case "audio/mp4": case "audio/x-m4a": case "audio/m4a": return ".m4a";
            case "audio/aac": return ".aac";
            case "audio/ogg": case "application/ogg": return ".ogg";
            case "audio/opus": return ".opus";
            case "audio/flac": case "audio/x-flac": return ".flac";
            case "audio/wav": case "audio/x-wav": case "audio/wave": return ".wav";
            default: return null;
        }
    }
}
