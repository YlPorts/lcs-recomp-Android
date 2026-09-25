package com.ylports.lcsrecomp;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
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
import java.nio.charset.StandardCharsets;
import java.nio.channels.FileChannel;
import java.util.Locale;

public final class MainActivity extends Activity {
    private static final int REQUEST_GAME_ISO = 1001;
    private static final int REQUEST_EBOOT_ELF = 1002;

    static {
        System.loadLibrary("lcsrecomp");
    }

    static native int nativeRun(String gameRoot, String configPath);
    static native void nativeSetSurface(android.view.Surface surface);
    static native void nativeSetInput(int buttons, int analogX, int analogY,
                                      int cameraX, int cameraY,
                                      boolean accelerate, boolean brake);
    static native void nativeRequestStop();

    private TextView statusView;
    private Button playButton;
    private Button elfButton;
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
        info.setText("v0.2 · ARM64 experimental\nSelecciona directamente tu ISO de GTA: Liberty City Stories\nUS v1.05 · ULUS-10041");
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
            boolean correctDisc = param != null
                    && (fileContainsAscii(param, "ULUS10041")
                    || fileContainsAscii(param, "ULUS-10041"));
            if (!correctDisc) {
                throw new IOException(
                        "La ISO no parece ser GTA LCS USA ULUS-10041. Este recompilado necesita esa versión.");
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

    private File ensureConfig() throws IOException {
        File config = new File(getFilesDir(), "LCSNative.ini");
        if (config.isFile()) return config;

        String text =
                "[Display]\n"
                + "Enabled=true\n"
                + "ResolutionMode=PSP\n"
                + "AspectRatio=Preserve\n"
                + "UpscaleFilter=Bilinear\n"
                + "IntegerScale=false\n"
                + "ShowFPS=true\n\n"
                + "[Rendering]\n"
                + "InternalResolutionMode=PSP\n"
                + "InternalScale=1\n"
                + "MSAA=1\n"
                + "HardwareTransform=false\n\n"
                + "[Audio]\n"
                + "Enabled=false\n\n"
                + "[Timing]\n"
                + "FrameRate=30\n\n"
                + "[Controls]\n"
                + "CameraStick=true\n"
                + "MouseSensitivity=12\n"
                + "CameraSmoothing=50\n"
                + "InvertCameraY=false\n"
                + "ModernControlScheme=false\n\n"
                + "[Widescreen]\n"
                + "Enabled=true\n";

        try (FileOutputStream out = new FileOutputStream(config)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
        return config;
    }

    private void startGame() {
        if (gameThread != null || !hasGameFiles()) return;

        final File config;
        try {
            config = ensureConfig();
        } catch (IOException ex) {
            statusView.setText("No se pudo crear la configuración: " + ex.getMessage());
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

        setContentView(root);

        SurfaceHolder holder = surfaceView.getHolder();
        holder.addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                nativeSetSurface(holder.getSurface());
                launchNativeThread(config);
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
                nativeSetSurface(holder.getSurface());
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
        nativeRequestStop();
        nativeSetSurface(null);
        super.onDestroy();
    }
}
