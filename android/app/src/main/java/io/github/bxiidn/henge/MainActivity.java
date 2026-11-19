package io.github.bxiidn.henge;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Development build. Reports actual native tests, without presenting a fake Mac desktop. */
public final class MainActivity extends Activity {
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    private static final String LIBRARY_ERROR;
    static {
        String error = null;
        try { System.loadLibrary("hengeandroid"); }
        catch (UnsatisfiedLinkError e) { error = e.getMessage(); }
        LIBRARY_ERROR = error;
    }
    private static native String nativeDiagnostics();
    private TextView result;
    private Button run;
    private String report = "Belum ada pengujian pada perangkat ini.";

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        if (saved != null) report = saved.getString("report", report);
        getWindow().setStatusBarColor(Color.rgb(12, 18, 29));
        getWindow().setNavigationBarColor(Color.rgb(12, 18, 29));
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(Color.rgb(12, 18, 29));
        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        int inset = dp(getResources().getConfiguration().screenWidthDp >= 600 ? 48 : 24);
        body.setPadding(inset, dp(32), inset, dp(32));
        body.setOnApplyWindowInsetsListener((v, insets) -> {
            v.setPadding(inset, dp(24) + insets.getSystemWindowInsetTop(), inset,
                dp(24) + insets.getSystemWindowInsetBottom());
            return insets;
        });
        scroll.addView(body);
        addText(body, "HENGE / ANDROID", 13, Color.rgb(112, 220, 189), true);
        addText(body, "Mesin mulai dipindahkan.", 30, Color.WHITE, true);
        addText(body, "Versi pengembangan 0.1\nHP dan tablet ARM64", 15, Color.LTGRAY, false);
        addText(body, "Belum mendukung boot macOS", 20, Color.rgb(255, 206, 130), true);
        addText(body, "APK ini menguji CPU/JIT Henge dan membaca informasi Vulkan perangkat. "
            + "Renderer macOS dan installer Android belum tersedia.", 16, Color.LTGRAY, false);
        addText(body, deviceInfo(), 15, Color.WHITE, false);
        run = new Button(this);
        run.setText(R.string.run_test);
        run.setAllCaps(false);
        body.addView(run, new LinearLayout.LayoutParams(-1, dp(56)));
        run.setOnClickListener(v -> runDiagnostics());
        result = addText(body, report, 15, Color.rgb(208, 221, 233), false);
        result.setTypeface(Typeface.MONOSPACE);
        result.setTextIsSelectable(true);
        Button copy = new Button(this);
        copy.setText(R.string.copy_result); copy.setAllCaps(false);
        copy.setOnClickListener(v -> {
            ClipboardManager clipboard = (ClipboardManager)getSystemService(CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText("Henge diagnostics", deviceInfo() + "\n\n" + report));
            Toast.makeText(this, "Hasil disalin", Toast.LENGTH_SHORT).show();
        });
        body.addView(copy);
        Button share = new Button(this);
        share.setText(R.string.share_result); share.setAllCaps(false);
        share.setOnClickListener(v -> {
            Intent send = new Intent(Intent.ACTION_SEND).setType("text/plain")
                .putExtra(Intent.EXTRA_TEXT, deviceInfo() + "\n\n" + report);
            startActivity(Intent.createChooser(send, "Bagikan hasil pengujian"));
        });
        body.addView(share);
        setContentView(scroll);
    }
    private void runDiagnostics() {
        run.setEnabled(false);
        result.setText(R.string.running);
        WORKER.execute(() -> {
            String next = LIBRARY_ERROR == null ? nativeDiagnostics() : "Gagal memuat mesin ARM64: " + LIBRARY_ERROR;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                report = next;
                result.setText(report);
                run.setEnabled(true);
            });
        });
    }
    private String deviceInfo() {
        ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
        ((ActivityManager)getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(info);
        return String.format(Locale.ROOT, "%s %s\nAndroid %s / API %d\nRAM total: %.1f GiB", Build.MANUFACTURER,
            Build.MODEL, Build.VERSION.RELEASE, Build.VERSION.SDK_INT, info.totalMem / 1073741824.0);
    }
    private TextView addText(LinearLayout parent, String text, int size, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(text); view.setTextSize(size); view.setTextColor(color);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        view.setPadding(0, 0, 0, dp(20));
        parent.addView(view, new LinearLayout.LayoutParams(-1, -2));
        return view;
    }
    private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    @Override protected void onSaveInstanceState(Bundle state) {
        state.putString("report", report);
        super.onSaveInstanceState(state);
    }
}
