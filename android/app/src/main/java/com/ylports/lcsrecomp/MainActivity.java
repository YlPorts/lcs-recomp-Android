package com.ylports.lcsrecomp;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.channels.FileChannel;
import java.util.Locale;
import java.util.List;

public final class MainActivity extends Activity {
    private static final int REQUEST_GAME_ISO = 1001;
    private static final int REQUEST_EBOOT_ELF = 1002;

    static {
        System.loadLibrary("lcsrecomp");
    }

    static native int nativeRun(String gameRoot, String configPath);
    static native void nativeSetSurface(android.view.Surface surface);
    static native void nativeSetDisplaySize(int width, int height);
    static native void nativeSetInput(int buttons, int analogX, int analogY,
                                      int cameraX, int cameraY,
                                      boolean accelerate, boolean brake);
    static native void nativeRequestStop();
    static native String nativeGetDebugStatus();

    private TextView statusView;
    private Button playButton;
    private Button elfButton;
    private TextView debugView;
    private final Handler debugHandler = new Handler(Looper.getMainLooper());
    private final Runnable debugPoll = new Runnable() {
        @Override
        public void run() {
            if (!inGame || debugView == null || destroyed) return;
            try {
                debugView.setText(nativeGetDebugStatus());
            } catch (Throwable error) {
                debugView.setText("DIAG error: " + error.getClass().getSimpleName());
            }
            debugHandler.postDelayed(this, 500L);
        }
    };
    private volatile Thread gameThread;
    private volatile boolean inGame;
    private boolean destroyed;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        enterImmersiveMode();
        showLauncher(null);
    }

    private void enterImmersiveMode() {
        Window window = getWindow();
        window.setStatusBarColor(Color.BLACK);
        window.setNavigationBarColor(Color.BLACK);
        window.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    private File gameDirectory() {
        return new File(getFilesDir(), "game");
    }

    private boolean hasGameData() {
        return new File(gameDirectory(), "PSP_GAME").isDirectory();
    }

    private boolean hasGameFiles() {
        File root = gameDirectory();
        return isElf(new File(root, "EBOOT.ELF"))
                && new File(root, "PSP_GAME").isDirectory();
    }

    private void showLauncher(String message) {
        inGame = false;
        debugHandler.removeCallbacks(debugPoll);
        debugView = null;
        enterImmersiveMode();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(48, 32, 48, 32);
        root.setBackgroundColor(Color.rgb(12, 14, 18));

        TextView title = new TextView(this);
        title.setText("LCS Recomp Android");
        title.setTextColor(Color.WHITE);
        title.setTextSize(28f);
        title.setGravity(Gravity.CENTER);
        root.addView(title, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        TextView info = new TextView(this);
        info.setText("v0.6.8 · GLES + audio fixes\nGTA: Liberty City Stories · ULUS-10041 v1.05");
        info.setTextColor(Color.LTGRAY);
        info.setTextSize(15f);
        info.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams infoParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        infoParams.topMargin = 18;
        root.addView(info, infoParams);

        statusView = new TextView(this);
        String defaultStatus;
        if (hasGameFiles()) {
            defaultStatus = "ISO preparada. Listo para jugar.";
        } else if (hasGameData()) {
            defaultStatus = "Datos del ISO importados. Falta un EBOOT.ELF desencriptado.";
        } else {
            defaultStatus = "Selecciona tu archivo .ISO.";
        }
        statusView.setText(message != null ? message : defaultStatus);
        statusView.setTextColor(Color.rgb(180, 210, 255));
        statusView.setTextSize(14f);
        statusView.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        statusParams.topMargin = 18;
        root.addView(statusView, statusParams);

        String previousCrash = loadPreviousCrashReport();
        if (previousCrash != null && !previousCrash.isEmpty()) {
            TextView crashView = new TextView(this);
            crashView.setText(previousCrash);
            crashView.setTextColor(Color.rgb(255, 210, 140));
            crashView.setTextSize(10f);
            crashView.setTextIsSelectable(true);
            crashView.setPadding(14, 10, 14, 10);
            crashView.setBackgroundColor(Color.rgb(34, 26, 18));
            LinearLayout.LayoutParams crashParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            crashParams.topMargin = 14;
            root.addView(crashView, crashParams);
        }

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        buttons.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams buttonsParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        buttonsParams.topMargin = 22;

        Button isoButton = new Button(this);
        isoButton.setText("Seleccionar ISO");
        isoButton.setOnClickListener(v -> chooseGameIso());
        buttons.addView(isoButton);

        elfButton = new Button(this);
        elfButton.setText("Añadir EBOOT.ELF");
        elfButton.setVisibility(hasGameData() && !hasGameFiles() ? View.VISIBLE : View.GONE);
        elfButton.setOnClickListener(v -> chooseEbootElf());
        LinearLayout.LayoutParams elfParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        elfParams.leftMargin = 12;
        buttons.addView(elfButton, elfParams);

        playButton = new Button(this);
        playButton.setText("Jugar");
        playButton.setEnabled(hasGameFiles());
        playButton.setOnClickListener(v -> startGame());
        LinearLayout.LayoutParams playParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        playParams.leftMargin = 12;
        buttons.addView(playButton, playParams);

        root.addView(buttons, buttonsParams);
        setContentView(root);
    }

    private String loadPreviousCrashReport() {
        File marker = new File(getFilesDir(), "native_crash.txt");
        File runtimeLog = new File(gameDirectory(), "android_runtime.log");
        String systemExit = loadSystemExitInfo();
        if (!marker.isFile() && !runtimeLog.isFile() && systemExit == null) return null;

        StringBuilder report = new StringBuilder();
        report.append("ÚLTIMO CIERRE / DIAGNÓSTICO\n");
        if (systemExit != null) report.append(systemExit).append('\n');
        try {
            if (marker.isFile()) {
                String signal = new String(Files.readAllBytes(marker.toPath()),
                        StandardCharsets.UTF_8).trim();
                report.append("Señal: ").append(signal.isEmpty() ? "(sin nombre)" : signal)
                        .append('\n');
            } else {
                report.append("Señal: no registrada\n");
            }
        } catch (IOException ex) {
            report.append("Señal: error leyendo marcador\n");
        }

        if (runtimeLog.isFile()) {
            try {
                byte[] data = Files.readAllBytes(runtimeLog.toPath());
                int keep = Math.min(data.length, 6000);
                int start = data.length - keep;
                String tail = new String(data, start, keep, StandardCharsets.UTF_8);
                report.append("\nÚltimas líneas:\n").append(tail);
            } catch (IOException ex) {
                report.append("\nNo se pudo leer android_runtime.log.");
            }
        }
        return report.toString();
    }

    private String loadSystemExitInfo() {
        if (Build.VERSION.SDK_INT < 30) return null;
        try {
            ActivityManager manager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
            if (manager == null) return null;
            List<ApplicationExitInfo> exits =
                    manager.getHistoricalProcessExitReasons(getPackageName(), 0, 3);
            if (exits == null || exits.isEmpty()) return null;

            ApplicationExitInfo info = exits.get(0);
            String description = info.getDescription();
            StringBuilder out = new StringBuilder();
            out.append("Android exit reason=").append(info.getReason())
                    .append(" status=").append(info.getStatus())
                    .append(" importance=").append(info.getImportance())
                    .append(" PSS=").append(info.getPss() / 1024).append(" MB")
                    .append(" RSS=").append(info.getRss() / 1024).append(" MB");
            if (description != null && !description.isEmpty())
                out.append("\nDescripción: ").append(description);
            return out.toString();
        } catch (Throwable ignored) {
            return null;
        }
    }

    private void chooseGameIso() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_GAME_ISO);
    }

    private void chooseEbootElf() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_EBOOT_ELF);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;

        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
        }

        if (requestCode == REQUEST_GAME_ISO) {
            String name = queryDisplayName(uri);
            if (name != null && !name.toLowerCase(Locale.ROOT).endsWith(".iso")) {
                statusView.setText("Ese archivo no termina en .iso. Selecciona la ISO de PSP.");
                return;
            }
            statusView.setText("Leyendo ISO...");
            playButton.setEnabled(false);
            elfButton.setVisibility(View.GONE);
            new Thread(() -> importGameIso(uri), "LCS-ISO-Import").start();
        } else if (requestCode == REQUEST_EBOOT_ELF) {
            statusView.setText("Comprobando EBOOT.ELF...");
            playButton.setEnabled(false);
            new Thread(() -> importEbootElf(uri), "LCS-ELF-Import").start();
        }
    }

    private void importGameIso(Uri isoUri) {
        File staging = new File(getFilesDir(), "game-importing");
        try {
            deleteRecursively(staging);
            if (!staging.mkdirs() && !staging.isDirectory())
                throw new IOException("No se pudo crear la carpeta temporal.");

            try (ParcelFileDescriptor descriptor =
                         getContentResolver().openFileDescriptor(isoUri, "r")) {
                if (descriptor == null) throw new IOException("No se pudo abrir el ISO.");
                try (FileInputStream input = new FileInputStream(descriptor.getFileDescriptor());
                     FileChannel channel = input.getChannel()) {
                    final long[] lastUiUpdate = {0L};
                    Iso9660Extractor.Result result = Iso9660Extractor.extract(
                            channel, staging, (path, extractedBytes) -> {
                                long now = SystemClock.uptimeMillis();
                                if (now - lastUiUpdate[0] < 450L) return;
                                lastUiUpdate[0] = now;
                                final String label = "Extrayendo: " + path
                                        + "\n" + formatMegabytes(extractedBytes) + " MB";
                                runOnUiThread(() -> {
                                    if (!destroyed && statusView != null)
                                        statusView.setText(label);
                                });
                            });
                    runOnUiThread(() -> {
                        if (!destroyed && statusView != null)
                            statusView.setText("ISO extraído. Verificando juego...");
                    });
                }
            }

            File pspGame = findCaseInsensitiveDirectory(staging, "PSP_GAME");
            if (pspGame == null)
                throw new IOException("El ISO no contiene PSP_GAME; no parece un juego de PSP.");

            if (!pspGame.getName().equals("PSP_GAME")) {
                File normalized = new File(staging, "PSP_GAME");
                if (!pspGame.renameTo(normalized))
                    throw new IOException("No se pudo normalizar PSP_GAME.");
                pspGame = normalized;
            }

            File param = findRelativeCaseInsensitive(pspGame, "PARAM.SFO");
            if (param == null)
                throw new IOException("La ISO no contiene PSP_GAME/PARAM.SFO.");

            String discId = readSfoString(param, "DISC_ID");
            String discVersion = readSfoString(param, "DISC_VERSION");
            if (!"ULUS10041".equalsIgnoreCase(discId != null ? discId.replace("-", "") : "")) {
                throw new IOException(
                        "ISO incorrecta: DISC_ID=" + String.valueOf(discId)
                                + ". Se necesita ULUS-10041.");
            }
            if (!"1.05".equals(discVersion)) {
                throw new IOException(
                        "Revisión incompatible: ULUS-10041 v" + String.valueOf(discVersion)
                                + ". Este recompilado necesita exactamente v1.05.");
            }

            String executableStatus = prepareExecutable(staging, pspGame);

            File destination = gameDirectory();
            deleteRecursively(destination);
            if (!staging.renameTo(destination)) {
                copyFileTree(staging, destination);
                deleteRecursively(staging);
            }

            final boolean ready = hasGameFiles();
            final String status;
            if (ready) {
                status = "ISO ULUS-10041 importada. " + executableStatus + "\nListo para jugar.";
            } else {
                status = "ISO ULUS-10041 importada.\n"
                        + "El EBOOT.BIN del disco está cifrado (~PSP), como en una copia comercial normal.\n"
                        + "Los datos ya están listos; añade un EBOOT.ELF desencriptado para arrancar.";
            }

            runOnUiThread(() -> {
                if (destroyed) return;
                statusView.setText(status);
                playButton.setEnabled(ready);
                elfButton.setVisibility(ready ? View.GONE : View.VISIBLE);
            });
        } catch (Exception ex) {
            deleteRecursively(staging);
            runOnUiThread(() -> {
                if (destroyed) return;
                statusView.setText("Error al importar ISO: " + ex.getMessage());
                playButton.setEnabled(hasGameFiles());
                elfButton.setVisibility(hasGameData() && !hasGameFiles()
                        ? View.VISIBLE : View.GONE);
            });
        }
    }

    private String prepareExecutable(File staging, File pspGame) throws IOException {
        File sysdir = findRelativeCaseInsensitive(pspGame, "SYSDIR");

        File[] candidates = new File[] {
                new File(staging, "EBOOT.ELF"),
                sysdir == null ? null : findRelativeCaseInsensitive(sysdir, "EBOOT.ELF"),
                sysdir == null ? null : findRelativeCaseInsensitive(sysdir, "BOOT.BIN"),
                sysdir == null ? null : findRelativeCaseInsensitive(sysdir, "EBOOT.BIN")
        };

        for (File candidate : candidates) {
            if (candidate != null && isElf(candidate)) {
                File target = new File(staging, "EBOOT.ELF");
                if (!candidate.getCanonicalPath().equals(target.getCanonicalPath()))
                    copyFile(candidate, target);
                return "Ejecutable ELF detectado automáticamente.";
            }
        }

        File encrypted = sysdir == null ? null : findRelativeCaseInsensitive(sysdir, "EBOOT.BIN");
        if (encrypted != null && hasMagic(encrypted, new byte[] {'~', 'P', 'S', 'P'}))
            return "EBOOT.BIN cifrado detectado.";

        return "No se encontró un ejecutable ELF compatible.";
    }

    private void importEbootElf(Uri uri) {
        if (!hasGameData()) {
            runOnUiThread(() -> {
                if (!destroyed) statusView.setText("Primero selecciona la ISO.");
            });
            return;
        }

        File temporary = new File(getFilesDir(), "EBOOT-importing.ELF");
        try {
            if (temporary.exists() && !temporary.delete())
                throw new IOException("No se pudo limpiar el archivo temporal.");

            try (InputStream in = getContentResolver().openInputStream(uri);
                 FileOutputStream out = new FileOutputStream(temporary)) {
                if (in == null) throw new IOException("No se pudo abrir el archivo.");
                byte[] buffer = new byte[1024 * 1024];
                for (int read; (read = in.read(buffer)) >= 0; ) {
                    if (read != 0) out.write(buffer, 0, read);
                }
            }

            if (!isElf(temporary))
                throw new IOException("El archivo seleccionado no es un ELF válido (falta la cabecera 7F ELF).");

            File target = new File(gameDirectory(), "EBOOT.ELF");
            copyFile(temporary, target);
            if (!temporary.delete()) temporary.deleteOnExit();

            runOnUiThread(() -> {
                if (destroyed) return;
                statusView.setText("EBOOT.ELF añadido. Listo para jugar.");
                playButton.setEnabled(true);
                elfButton.setVisibility(View.GONE);
            });
        } catch (Exception ex) {
            //noinspection ResultOfMethodCallIgnored
            temporary.delete();
            runOnUiThread(() -> {
                if (destroyed) return;
                statusView.setText("Error con EBOOT.ELF: " + ex.getMessage());
                playButton.setEnabled(hasGameFiles());
            });
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(
                uri, new String[] {OpenableColumns.DISPLAY_NAME},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) return cursor.getString(0);
        } catch (Exception ignored) {
        }
        return null;
    }

    private static String formatMegabytes(long bytes) {
        return String.format(Locale.ROOT, "%.1f", bytes / (1024.0 * 1024.0));
    }

    private static File findCaseInsensitiveDirectory(File parent, String wanted) {
        File file = findRelativeCaseInsensitive(parent, wanted);
        return file != null && file.isDirectory() ? file : null;
    }

    private static File findRelativeCaseInsensitive(File parent, String wanted) {
        if (parent == null || !parent.isDirectory()) return null;
        File exact = new File(parent, wanted);
        if (exact.exists()) return exact;
        File[] files = parent.listFiles();
        if (files == null) return null;
        for (File file : files)
            if (file.getName().equalsIgnoreCase(wanted)) return file;
        return null;
    }

    private static String readSfoString(File file, String wantedKey) throws IOException {
        byte[] data = Files.readAllBytes(file.toPath());
        if (data.length < 20
                || data[0] != 0x00 || data[1] != 0x50
                || data[2] != 0x53 || data[3] != 0x46) {
            throw new IOException("PARAM.SFO inválido.");
        }

        ByteBuffer buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        int keyTableOffset = buffer.getInt(8);
        int dataTableOffset = buffer.getInt(12);
        int entryCount = buffer.getInt(16);
        if (keyTableOffset < 0 || dataTableOffset < 0 || entryCount < 0
                || keyTableOffset >= data.length || dataTableOffset >= data.length
                || 20L + (long) entryCount * 16L > data.length) {
            throw new IOException("Tabla PARAM.SFO inválida.");
        }

        for (int i = 0; i < entryCount; i++) {
            int entry = 20 + i * 16;
            int keyOffset = buffer.getShort(entry) & 0xFFFF;
            int valueLength = buffer.getInt(entry + 4);
            int valueOffset = buffer.getInt(entry + 12);

            int keyStart = keyTableOffset + keyOffset;
            if (keyStart < 0 || keyStart >= data.length) continue;
            int keyEnd = keyStart;
            while (keyEnd < data.length && data[keyEnd] != 0) keyEnd++;
            String key = new String(data, keyStart, keyEnd - keyStart, StandardCharsets.UTF_8);
            if (!wantedKey.equals(key)) continue;

            int valueStart = dataTableOffset + valueOffset;
            if (valueStart < 0 || valueStart >= data.length || valueLength <= 0) return null;
            int available = Math.min(valueLength, data.length - valueStart);
            int valueEnd = valueStart;
            int limit = valueStart + available;
            while (valueEnd < limit && data[valueEnd] != 0) valueEnd++;
            return new String(data, valueStart, valueEnd - valueStart, StandardCharsets.UTF_8);
        }
        return null;
    }

    private static boolean fileContainsAscii(File file, String needle) throws IOException {
        if (file == null || !file.isFile()) return false;
        byte[] target = needle.getBytes(StandardCharsets.US_ASCII);
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] data = new byte[256 * 1024];
            int used = 0;
            for (int read; (read = in.read(data, used, data.length - used)) > 0; ) {
                used += read;
                if (indexOf(data, used, target) >= 0) return true;
                if (used == data.length) break;
            }
        }
        return false;
    }

    private static int indexOf(byte[] data, int length, byte[] target) {
        outer:
        for (int i = 0; i + target.length <= length; i++) {
            for (int j = 0; j < target.length; j++)
                if (data[i + j] != target[j]) continue outer;
            return i;
        }
        return -1;
    }

    private static boolean isElf(File file) {
        return hasMagic(file, new byte[] {0x7F, 'E', 'L', 'F'});
    }

    private static boolean hasMagic(File file, byte[] magic) {
        if (file == null || !file.isFile() || file.length() < magic.length) return false;
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] header = new byte[magic.length];
            int total = 0;
            while (total < header.length) {
                int read = in.read(header, total, header.length - total);
                if (read < 0) return false;
                total += read;
            }
            for (int i = 0; i < magic.length; i++)
                if (header[i] != magic[i]) return false;
            return true;
        } catch (IOException ignored) {
            return false;
        }
    }

    private static void copyFile(File source, File destination) throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs())
            throw new IOException("No se pudo crear " + parent);
        try (FileInputStream in = new FileInputStream(source);
             FileOutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[1024 * 1024];
            for (int read; (read = in.read(buffer)) >= 0; )
                if (read != 0) out.write(buffer, 0, read);
        }
    }

    private static void copyFileTree(File source, File destination) throws IOException {
        if (source.isDirectory()) {
            if (!destination.mkdirs() && !destination.isDirectory())
                throw new IOException("No se pudo crear " + destination);
            File[] children = source.listFiles();
            if (children != null) {
                for (File child : children)
                    copyFileTree(child, new File(destination, child.getName()));
            }
            return;
        }
        copyFile(source, destination);
    }

    private static void deleteRecursively(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null)
                for (File child : children) deleteRecursively(child);
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    private void validateInstalledGameRevision() throws IOException {
        File pspGame = findCaseInsensitiveDirectory(gameDirectory(), "PSP_GAME");
        if (pspGame == null) throw new IOException("Falta PSP_GAME.");
        File param = findRelativeCaseInsensitive(pspGame, "PARAM.SFO");
        if (param == null) throw new IOException("Falta PSP_GAME/PARAM.SFO.");

        String discId = readSfoString(param, "DISC_ID");
        String discVersion = readSfoString(param, "DISC_VERSION");
        String normalizedId = discId != null ? discId.replace("-", "") : "";
        if (!"ULUS10041".equalsIgnoreCase(normalizedId))
            throw new IOException("DISC_ID=" + String.valueOf(discId)
                    + "; se necesita ULUS-10041.");
        if (!"1.05".equals(discVersion))
            throw new IOException("tu ISO es ULUS-10041 v" + String.valueOf(discVersion)
                    + "; el recompilado requiere exactamente v1.05.");
    }

    private File ensureConfig() throws IOException {
        File config = new File(getFilesDir(), "LCSNative.ini");

        // Rewrite the bootstrap config when launching so users upgrading from
        // the diagnostic builds do not remain stuck with audio disabled or
        // letterboxed presentation settings.
        String text =
                "[Display]\n"
                + "Enabled=true\n"
                + "ResolutionMode=PSP\n"
                + "AspectRatio=Stretch\n"
                + "UpscaleFilter=Bilinear\n"
                + "IntegerScale=false\n"
                + "ShowFPS=false\n\n"
                + "[Rendering]\n"
                + "InternalResolutionMode=Scale\n"
                + "InternalScale=1\n"
                + "MSAA=1\n"
                + "HardwareTransform=false\n\n"
                + "[Audio]\n"
                + "Enabled=true\n"
                + "Volume=100\n\n"
                + "[Timing]\n"
                + "FrameRate=60\n\n"
                + "[Controls]\n"
                + "CameraStick=true\n"
                + "MouseSensitivity=12\n"
                + "CameraSmoothing=50\n"
                + "InvertCameraY=false\n"
                + "ModernControlScheme=false\n\n"
                + "[Widescreen]\n"
                + "Enabled=true\n\n"
                + "[Diagnostics]\n"
                + "LogToFile=true\n"
                + "LogFile=android_runtime.log\n"
                + "FlushEveryLine=true\n";

        try (FileOutputStream out = new FileOutputStream(config)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
        return config;
    }

    private void startGame() {
        if (gameThread != null || !hasGameFiles()) return;

        final File config;
        try {
            validateInstalledGameRevision();
            config = ensureConfig();
        } catch (IOException ex) {
            statusView.setText("No se puede iniciar: " + ex.getMessage());
            return;
        }

        inGame = true;
        enterImmersiveMode();

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);

        SurfaceView surfaceView = new SurfaceView(this);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        TouchControlsView controls = new TouchControlsView(this);
        root.addView(controls, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        // Runtime diagnostics remain available natively, but the normal mobile
        // build no longer draws the large diagnostic overlay over gameplay.
        debugView = null;
        debugHandler.removeCallbacks(debugPoll);
        setContentView(root);

        SurfaceHolder holder = surfaceView.getHolder();
        holder.addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                nativeSetSurface(holder.getSurface());
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                // Capture the physical SurfaceView size only before native
                // execution starts. ANativeWindow later switches its producer
                // buffer to the PSP render resolution and may trigger another
                // surfaceChanged; that must not replace the phone aspect ratio.
                if (gameThread == null) nativeSetDisplaySize(width, height);
                nativeSetSurface(holder.getSurface());
                launchNativeThread(config);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nativeSetSurface(null);
            }
        });
    }

    private synchronized void launchNativeThread(File config) {
        if (gameThread != null) return;
        final String gameRoot = gameDirectory().getAbsolutePath();
        final String configPath = config.getAbsolutePath();

        gameThread = new Thread(null, () -> {
            int result = nativeRun(gameRoot, configPath);
            synchronized (MainActivity.this) {
                gameThread = null;
            }
            runOnUiThread(() -> {
                if (destroyed) return;
                nativeSetSurface(null);
                showLauncher(result == 0
                        ? "El juego se cerró."
                        : "LCSNative terminó con código " + result + ".");
            });
        }, "LCSNative", 32L * 1024L * 1024L);
        gameThread.start();
    }

    @Override
    public void onBackPressed() {
        if (inGame) {
            nativeRequestStop();
            return;
        }
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        debugHandler.removeCallbacks(debugPoll);
        nativeRequestStop();
        nativeSetSurface(null);
        super.onDestroy();
    }
}
