package com.saturn64.mobile;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.util.zip.CRC32;

/**
 * Start screen (Saturn background + logo).
 *
 * - Saturn keeps ALL its files (dynos, models, textures, res, config, saves, logs) in
 *   /storage/emulated/0/com.amoggo64.saturn, like the PC version. This needs "All files access".
 *   If the user refuses, the app falls back to its private storage.
 * - "Import ROM" opens the phone's file picker; the ROM is copied to <data>/sm64.z64 and verified
 *   (size + CRC32). Saturn extracts the assets from it on the first run.
 * - Once the assets are extracted (marker res/.extracted written by Saturn), this screen is skipped on
 *   later launches and the game starts directly. If the last run crashed (crash.txt exists) the screen is
 *   shown again so the debug log can be saved.
 */
public class ImportActivity extends Activity {

    private static final int REQ_PICK_ROM = 1001;
    private static final int REQ_SAVE_LOG = 1002;
    private static final int REQ_STORAGE = 1003;
    private static final long ROM_SIZE = 8L * 1024 * 1024;
    private static final long ROM_CRC32 = 0x3CE60709L;
    private static final String DATA_FOLDER = "com.amoggo64.saturn";

    private TextView status;
    private Button importButton;
    private Button continueButton;
    private Button shareButton;

    private boolean busy = false;        // an import / preparation is running
    private boolean launching = false;   // the game is being started
    private boolean noAutoLaunch = false;

    // ------------------------------------------------------------------ storage

    private SharedPreferences prefs() {
        return getSharedPreferences("saturn", MODE_PRIVATE);
    }

    private boolean usePrivate() {
        return prefs().getBoolean("private_storage", false);
    }

    private boolean hasExternalAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        if (Build.VERSION.SDK_INT >= 23) {
            return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    private boolean storageReady() {
        return usePrivate() || hasExternalAccess();
    }

    private File dataDir() {
        if (!usePrivate() && hasExternalAccess()) {
            return new File(Environment.getExternalStorageDirectory(), DATA_FOLDER);
        }
        return getFilesDir();
    }

    /** Creates the data folder and tells the native side where it is (datadir.txt). */
    private void prepareDataDir() {
        File d = dataDir();
        d.mkdirs();
        if (!d.isDirectory() || !d.canWrite()) {
            prefs().edit().putBoolean("private_storage", true).apply();
            d = getFilesDir();
        }
        try (OutputStream out = new FileOutputStream(new File(getFilesDir(), "datadir.txt"))) {
            out.write(d.getAbsolutePath().getBytes("UTF-8"));
        } catch (IOException e) {
            // the native side falls back to the internal folder
        }
        migrateOldRom(d);
    }

    /** Earlier builds kept the ROM in the private folder: reuse it so it does not have to be imported again. */
    private void migrateOldRom(File dir) {
        File old = new File(getFilesDir(), "sm64.z64");
        File dst = new File(dir, "sm64.z64");
        if (dir.equals(getFilesDir()) || dst.exists() || !old.exists() || old.length() != ROM_SIZE) return;
        try (InputStream in = new java.io.FileInputStream(old); OutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } catch (IOException e) {
            dst.delete();
        }
    }

    private void requestStorageAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            try {
                startActivityForResult(i, REQ_STORAGE);
            } catch (Exception e) {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), REQ_STORAGE);
            }
        } else if (Build.VERSION.SDK_INT >= 23) {
            requestPermissions(new String[] { Manifest.permission.WRITE_EXTERNAL_STORAGE,
                    Manifest.permission.READ_EXTERNAL_STORAGE }, REQ_STORAGE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        refreshUi();
    }

    private File romFile() {
        return new File(dataDir(), "sm64.z64");
    }

    private File extractedMarker() {
        return new File(new File(dataDir(), "res"), ".extracted");
    }

    // ------------------------------------------------------------------ UI

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private Button makeButton(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(Color.WHITE);
        b.setTextSize(17);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0x55FFFFFF);
        bg.setCornerRadius(dp(14));
        bg.setStroke(dp(2), 0xCCFFFFFF);
        b.setBackground(bg);
        b.setMinWidth(dp(190));
        b.setMinimumHeight(dp(52));
        b.setPadding(dp(18), dp(8), dp(18), dp(8));
        return b;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);

        ImageView bg = new ImageView(this);
        bg.setImageResource(R.drawable.saturn_bg);
        bg.setScaleType(ImageView.ScaleType.CENTER_CROP);
        root.addView(bg, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        View shade = new View(this);
        GradientDrawable g = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] { 0x00000000, 0xE6000000 });
        shade.setBackground(g);
        FrameLayout.LayoutParams shadeLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, 0, Gravity.BOTTOM);
        shadeLp.height = Math.round(getResources().getDisplayMetrics().heightPixels * 0.50f);
        root.addView(shade, shadeLp);

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER_HORIZONTAL);
        box.setPadding(dp(24), dp(8), dp(24), dp(18));

        status = new TextView(this);
        status.setTextColor(Color.WHITE);
        status.setTextSize(15);
        status.setGravity(Gravity.CENTER);
        status.setShadowLayer(6f, 0f, 2f, Color.BLACK);
        LinearLayout.LayoutParams stLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        stLp.bottomMargin = dp(12);
        box.addView(status, stLp);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);

        importButton = makeButton("Import ROM");
        importButton.setOnClickListener(v -> onImportClicked());
        LinearLayout.LayoutParams bLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        bLp.rightMargin = dp(10);
        row.addView(importButton, bLp);

        continueButton = makeButton("Continue");
        continueButton.setOnClickListener(v -> onContinueClicked());
        LinearLayout.LayoutParams cLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        cLp.leftMargin = dp(10);
        row.addView(continueButton, cLp);

        shareButton = makeButton("Share log");
        shareButton.setOnClickListener(v -> shareLog());
        LinearLayout.LayoutParams shLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        shLp.leftMargin = dp(10);
        row.addView(shareButton, shLp);
        shareButton.setVisibility(View.GONE);

        box.addView(row);

        Button logButton = new Button(this);
        logButton.setText("Save debug log");
        logButton.setAllCaps(false);
        logButton.setTextColor(0xCCFFFFFF);
        logButton.setTextSize(13);
        logButton.setBackgroundColor(Color.TRANSPARENT);
        logButton.setOnClickListener(v -> saveLog());
        LinearLayout.LayoutParams lgLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lgLp.topMargin = dp(4);
        box.addView(logButton, lgLp);

        root.addView(box, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM));

        setContentView(root);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshUi();
    }

    private void setEnabledLook(Button b, boolean enabled) {
        b.setEnabled(enabled);
        b.setAlpha(enabled ? 1f : 0.45f);
    }

    private void refreshUi() {
        if (launching || busy) return;

        if (!storageReady()) {
            status.setText("Saturn keeps its files (models, textures, DynOS packs...) in /storage/emulated/0/"
                    + DATA_FOLDER + ", like on PC. Allow access to all files to continue.");
            importButton.setText("Grant access");
            continueButton.setText("Use private storage");
            setEnabledLook(importButton, true);
            setEnabledLook(continueButton, true);
            return;
        }

        prepareDataDir();
        File crash = new File(dataDir(), "crash.txt");
        boolean abnormal = new File(getFilesDir(), "running.flag").exists();   // last run did not end normally
        boolean extracted = extractedMarker().exists();

        // The ROM was already loaded on a previous run: go straight to the game.
        if (extracted && !crash.exists() && !abnormal && !noAutoLaunch) {
            startEngine();
            return;
        }

        File rom = romFile();
        boolean romReady = rom.exists() && rom.length() == ROM_SIZE;
        boolean canContinue = romReady || extracted;

        shareButton.setVisibility((crash.exists() || abnormal) ? View.VISIBLE : View.GONE);
        if (crash.exists() || abnormal) {
            status.setText("Saturn did not close normally last time. Tap \"Share log\" to send the log to the developer, or Continue to try again.");
        } else if (canContinue) {
            status.setText("ROM ready.");
        } else {
            status.setText("Import your Super Mario 64 ROM (US, .z64) to get started.");
        }
        importButton.setText(canContinue ? "Import another ROM" : "Import ROM");
        continueButton.setText("Continue");
        setEnabledLook(importButton, true);
        setEnabledLook(continueButton, canContinue);
    }

    private void onImportClicked() {
        if (!storageReady()) {
            requestStorageAccess();
        } else {
            openPicker();
        }
    }

    private void onContinueClicked() {
        if (!storageReady()) {
            prefs().edit().putBoolean("private_storage", true).apply();
            refreshUi();
        } else {
            startEngine();
        }
    }

    private void openPicker() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, REQ_PICK_ROM);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_SAVE_LOG) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) writeLog(data.getData());
            return;
        }
        if (requestCode != REQ_PICK_ROM || resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        final Uri uri = data.getData();
        busy = true;
        status.setText("Importing and verifying...");
        setEnabledLook(importButton, false);
        setEnabledLook(continueButton, false);
        new Thread(() -> {
            final String error = importRom(uri);
            runOnUiThread(() -> {
                busy = false;
                if (error != null) {
                    Toast.makeText(this, error, Toast.LENGTH_LONG).show();
                }
                noAutoLaunch = true; // let the user press Continue after importing
                refreshUi();
            });
        }).start();
    }

    /** Returns null on success, or the error message. */
    private String importRom(Uri uri) {
        File dir = dataDir();
        dir.mkdirs();
        File tmp = new File(dir, "sm64.import.tmp");
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream out = new FileOutputStream(tmp)) {
            if (in == null) return "Could not open the file.";
            CRC32 crc = new CRC32();
            byte[] buf = new byte[64 * 1024];
            long total = 0;
            int n;
            while ((n = in.read(buf)) > 0) {
                total += n;
                if (total > ROM_SIZE) {
                    tmp.delete();
                    return "Invalid ROM: the file is too large (it must be 8 MB).";
                }
                crc.update(buf, 0, n);
                out.write(buf, 0, n);
            }
            out.flush();
            if (total != ROM_SIZE) {
                tmp.delete();
                return "Invalid ROM: the size is not 8 MB.";
            }
            if (crc.getValue() != ROM_CRC32) {
                tmp.delete();
                return "Invalid ROM: use an unmodified US ROM in .z64 format.";
            }
        } catch (IOException | SecurityException e) {
            tmp.delete();
            return "Error copying the ROM: " + e.getMessage();
        }
        File dst = romFile();
        if (dst.exists() && !dst.delete()) {
            tmp.delete();
            return "Could not replace the previous ROM.";
        }
        if (!tmp.renameTo(dst)) {
            tmp.delete();
            return "Could not save the ROM.";
        }
        return null;
    }

    private void startEngine() {
        if (launching) return;
        launching = true;
        busy = true;
        status.setText("Preparing Saturn files...");
        setEnabledLook(importButton, false);
        setEnabledLook(continueButton, false);
        final File dir = dataDir();
        new Thread(() -> {
            String error = null;
            try {
                AssetInstaller.install(getAssets(), dir);
                new File(dir, "crash.txt").delete(); // the previous crash has been acknowledged
                new File(getFilesDir(), "running.flag").delete();
            } catch (IOException e) {
                error = "Error preparing the files: " + e.getMessage();
            }
            final String err = error;
            runOnUiThread(() -> {
                if (err != null) {
                    launching = false;
                    busy = false;
                    noAutoLaunch = true;
                    Toast.makeText(this, err, Toast.LENGTH_LONG).show();
                    refreshUi();
                } else {
                    startActivity(new Intent(this, SaturnActivity.class));
                    finish();
                }
            });
        }).start();
    }

    // ------------------------------------------------------------------ debug log

    private void saveLog() {
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("text/plain");
        i.putExtra(Intent.EXTRA_TITLE, "saturn_log.txt");
        startActivityForResult(i, REQ_SAVE_LOG);
    }

    private static String tail(File f, int maxBytes) {
        if (!f.exists()) return "(missing: " + f.getName() + ")\n";
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            long len = raf.length();
            int n = (int) Math.min(len, maxBytes);
            byte[] b = new byte[n];
            raf.seek(len - n);
            raf.readFully(b);
            return new String(b, "UTF-8");
        } catch (IOException e) {
            return "(could not read " + f.getName() + ": " + e.getMessage() + ")\n";
        }
    }

    private String buildLogText(int logBytes, int prevBytes, int crashBytes) {
        File dir = dataDir();
        StringBuilder sb = new StringBuilder();
        sb.append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append('\n');
        sb.append("Android: ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        sb.append("ABIs: ").append(TextUtils.join(", ", Build.SUPPORTED_ABIS)).append('\n');
        sb.append("Data folder: ").append(dir.getAbsolutePath()).append("\n\n");
        sb.append("===== crash.txt =====\n").append(tail(new File(dir, "crash.txt"), crashBytes));
        sb.append("\n===== saturn_log.txt (last run) =====\n").append(tail(new File(dir, "saturn_log.txt"), logBytes));
        if (prevBytes > 0) {
            sb.append("\n===== saturn_log_prev.txt (run before) =====\n").append(tail(new File(dir, "saturn_log_prev.txt"), prevBytes));
        }
        return sb.toString();
    }

    private void writeLog(Uri uri) {
        String text = buildLogText(150000, 40000, 20000);
        try (OutputStream out = getContentResolver().openOutputStream(uri)) {
            if (out == null) throw new IOException("no output stream");
            out.write(text.getBytes("UTF-8"));
            Toast.makeText(this, "Log saved.", Toast.LENGTH_SHORT).show();
        } catch (IOException e) {
            Toast.makeText(this, "Could not save the log: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    /** Shares the end of the log as plain text (WhatsApp, Telegram, e-mail...). */
    private void shareLog() {
        String text = buildLogText(30000, 0, 8000);
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("text/plain");
        send.putExtra(Intent.EXTRA_SUBJECT, "Saturn Android log");
        send.putExtra(Intent.EXTRA_TEXT, text);
        startActivity(Intent.createChooser(send, "Share log"));
    }
}
