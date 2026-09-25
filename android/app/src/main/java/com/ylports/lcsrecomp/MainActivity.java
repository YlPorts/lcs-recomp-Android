package com.ylports.lcsrecomp;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
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
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

public final class MainActivity extends Activity {
    private static final int REQUEST_GAME_TREE = 1001;

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

    private boolean hasGameFiles() {
        File root = gameDirectory();
        return new File(root, "EBOOT.ELF").isFile()
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
        info.setText("Versión ARM64 experimental\nImporta tu copia US v1.05 (ULUS-10041): EBOOT.ELF + PSP_GAME");
        info.setTextColor(Color.LTGRAY);
        info.setTextSize(15f);
        info.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams infoParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        infoParams.topMargin = 18;
        root.addView(info, infoParams);

        statusView = new TextView(this);
        statusView.setText(message != null ? message
                : (hasGameFiles() ? "Archivos del juego listos." : "Aún no has importado el juego."));
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

        Button importButton = new Button(this);
        importButton.setText("Importar juego");
        importButton.setOnClickListener(v -> chooseGameFolder());
        buttons.addView(importButton);

        playButton = new Button(this);
        playButton.setText("Jugar");
        playButton.setEnabled(hasGameFiles());
        playButton.setOnClickListener(v -> startGame());
        LinearLayout.LayoutParams playParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        playParams.leftMargin = 16;
        buttons.addView(playButton, playParams);

        root.addView(buttons, buttonsParams);
        setContentView(root);
    }

    private void chooseGameFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_GAME_TREE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_TREE || resultCode != RESULT_OK
                || data == null || data.getData() == null) {
            return;
        }

        Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(
                    tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) {
        }

        statusView.setText("Importando archivos...");
        playButton.setEnabled(false);
        new Thread(() -> importGameTree(tree), "LCS-Import").start();
    }

    private void importGameTree(Uri treeUri) {
        File staging = new File(getFilesDir(), "game-importing");
        try {
            deleteRecursively(staging);
            if (!staging.mkdirs() && !staging.isDirectory())
                throw new IOException("No se pudo crear la carpeta temporal.");

            String rootId = DocumentsContract.getTreeDocumentId(treeUri);
            copyDocumentDirectory(treeUri, rootId, staging);

            File eboot = new File(staging, "EBOOT.ELF");
            File pspGame = new File(staging, "PSP_GAME");
            if (!eboot.isFile() || !pspGame.isDirectory()) {
                throw new IOException(
                        "La carpeta elegida debe contener EBOOT.ELF y la carpeta PSP_GAME.");
            }

            File destination = gameDirectory();
            deleteRecursively(destination);
            if (!staging.renameTo(destination)) {
                copyFileTree(staging, destination);
                deleteRecursively(staging);
            }

            runOnUiThread(() -> {
                if (destroyed) return;
                statusView.setText("Juego importado. Listo para arrancar.");
                playButton.setEnabled(true);
            });
        } catch (Exception ex) {
            deleteRecursively(staging);
            runOnUiThread(() -> {
                if (destroyed) return;
                statusView.setText("Error: " + ex.getMessage());
                playButton.setEnabled(hasGameFiles());
            });
        }
    }

    private void copyDocumentDirectory(Uri treeUri, String parentDocumentId, File destination)
            throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                treeUri, parentDocumentId);
        String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE
        };

        try (Cursor cursor = getContentResolver().query(
                children, projection, null, null, null)) {
            if (cursor == null) throw new IOException("No se pudo leer la carpeta seleccionada.");
            while (cursor.moveToNext()) {
                String documentId = cursor.getString(0);
                String displayName = sanitizeName(cursor.getString(1));
                String mime = cursor.getString(2);
                File output = new File(destination, displayName);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!output.mkdirs() && !output.isDirectory())
                        throw new IOException("No se pudo crear " + displayName);
                    copyDocumentDirectory(treeUri, documentId, output);
                } else {
                    Uri document = DocumentsContract.buildDocumentUriUsingTree(
                            treeUri, documentId);
                    copyUri(document, output);
                }
            }
        }
    }

    private void copyUri(Uri source, File destination) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(source);
             FileOutputStream out = new FileOutputStream(destination)) {
            if (in == null) throw new IOException("No se pudo abrir " + source);
            byte[] buffer = new byte[1024 * 1024];
            for (int read; (read = in.read(buffer)) >= 0; ) {
                if (read != 0) out.write(buffer, 0, read);
            }
        }
    }

    private static String sanitizeName(String name) {
        if (name == null || name.isEmpty()) return "unnamed";
        return name.replace('/', '_').replace('\\', '_');
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
        try (InputStream in = new java.io.FileInputStream(source);
             FileOutputStream out = new FileOutputStream(destination)) {
            byte[] buffer = new byte[1024 * 1024];
            for (int read; (read = in.read(buffer)) >= 0; )
                if (read != 0) out.write(buffer, 0, read);
        }
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
