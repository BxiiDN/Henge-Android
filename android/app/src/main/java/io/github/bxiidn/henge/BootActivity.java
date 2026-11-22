package io.github.bxiidn.henge;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** A bounded serial boot laboratory. Selected images are opened read-only. */
public final class BootActivity extends Activity {
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
    private static final String LIBRARY_ERROR;
    static {
        String error = null;
        try { System.loadLibrary("hengeandroid"); }
        catch (UnsatisfiedLinkError e) { error = e.getMessage(); }
        LIBRARY_ERROR = error;
    }
    private static native void nativePrepare();
    private static native void nativeStop();
    private static native String nativeSerial();
    private static native String nativeBoot(int rom, int disk, int aux, int ram,
                                           long ecid, long seed, boolean selftest);
    // These contain no Activity reference and survive rotation while the worker runs.
    private static volatile boolean busy;
    private static volatile String report = "Belum ada uji board. Jalankan uji bawaan terlebih dahulu.";
    private static final Uri[] selected = new Uri[3];
    private static final String[] selectedNames = new String[3];
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Button[] pickers = new Button[3];
    private Button test, boot, stop;
    private Spinner ram;
    private EditText ecid, seed;
    private TextView output;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { render(); handler.postDelayed(this, 500); }
    };
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true); scroll.setBackgroundColor(Color.rgb(12,18,29));
        LinearLayout body = new LinearLayout(this); body.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(getResources().getConfiguration().screenWidthDp >= 600 ? 40 : 20);
        body.setPadding(pad,dp(24),pad,dp(24));
        body.setOnApplyWindowInsetsListener((v,i) -> {
            v.setPadding(pad,dp(24)+i.getSystemWindowInsetTop(),pad,dp(24)+i.getSystemWindowInsetBottom()); return i;
        });
        scroll.addView(body);
        text(body,"Henge 0.2 · Boot serial",24);
        text(body,"Uji board bawaan memeriksa RAM, disk virtual dan UART dengan firmware uji Henge. Tidak memerlukan file macOS.",16);
        test = button(body,"Jalankan uji board",() -> start(true));
        text(body,"Percobaan ROM eksternal",20);
        text(body,"Pilih ROM raw AVPBooter, disk raw dan auxiliary.bin yang cocok. ECID dan seed harus sesuai identitas VM asal. APK tidak menyertakan file macOS atau installer.",15);
        String[] labels={"Pilih ROM raw (.bin)","Pilih disk raw (.img)","Pilih auxiliary.bin"};
        for(int i=0;i<3;i++) { final int index=i; pickers[i]=button(body,labels[i],() -> pick(index)); }
        text(body,"RAM guest (MiB)",15);
        ram = new Spinner(this); ram.setId(R.id.boot_ram);
        ram.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"512","1024","2048"}));
        body.addView(ram);
        ecid = field(body,"ECID VM asal (angka desimal atau 0x...)",R.id.boot_ecid);
        seed = field(body,"Seed VM asal (angka desimal atau 0x...)",R.id.boot_seed);
        text(body,"Percobaan berlangsung maksimal 30 detik. File disk dan aux hanya dibaca. Mode ini belum memiliki GPU, tampilan macOS, audio, jaringan atau restore. Disk yang membutuhkan penulisan bisa gagal boot.",15);
        boot = button(body,"Coba boot ROM · 30 detik",() -> start(false));
        stop = button(body,"Hentikan",() -> { if(LIBRARY_ERROR==null) nativeStop(); });
        output = text(body,report,14); output.setTypeface(Typeface.MONOSPACE); output.setTextIsSelectable(true);
        button(body,"Salin hasil",() -> ((ClipboardManager)getSystemService(CLIPBOARD_SERVICE))
            .setPrimaryClip(ClipData.newPlainText("Henge serial", report)));
        button(body,"Bagikan hasil",() -> startActivity(Intent.createChooser(new Intent(Intent.ACTION_SEND)
            .setType("text/plain").putExtra(Intent.EXTRA_TEXT,report),"Bagikan log serial")));
        setContentView(scroll); render();
    }
    private void pick(int index) {
        Intent intent=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent,100+index);
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request>=100 && request<103 && result==RESULT_OK && data!=null && data.getData()!=null) {
            selected[request-100]=data.getData();
            String name=data.getData().getLastPathSegment();
            try(android.database.Cursor cursor=getContentResolver().query(data.getData(),
                    new String[]{android.provider.OpenableColumns.DISPLAY_NAME},null,null,null)) {
                if(cursor!=null && cursor.moveToFirst()) name=cursor.getString(0);
            } catch(RuntimeException ignored) { /* Fall back to the provider document name. */ }
            selectedNames[request-100]=name==null ? "file dipilih" : name;
            try { getContentResolver().takePersistableUriPermission(data.getData(),Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch(SecurityException ignored) { /* Current grant still permits this session. */ }
            render();
        }
    }
    private static long number(String input) {
        String s=input.trim();
        if(s.isEmpty()) throw new NumberFormatException();
        return s.startsWith("0x") || s.startsWith("0X") ? Long.parseUnsignedLong(s.substring(2),16) : Long.parseUnsignedLong(s);
    }
    private void start(boolean selftest) {
        if(busy) return;
        if(LIBRARY_ERROR!=null) { report="Gagal memuat mesin: "+LIBRARY_ERROR; render(); return; }
        long machineEcid=0, machineSeed=0;
        int memory=selftest ? 64 : Integer.parseInt((String)ram.getSelectedItem());
        if(!selftest) {
            if(selected[0]==null || selected[1]==null || selected[2]==null) { report="Pilih ketiga file dahulu."; render(); return; }
            try { machineEcid=number(ecid.getText().toString()); machineSeed=number(seed.getText().toString()); }
            catch(NumberFormatException e) { report="Isi ECID dan seed VM asal dengan angka yang valid."; render(); return; }
        }
        ActivityManager.MemoryInfo info=new ActivityManager.MemoryInfo();
        ((ActivityManager)getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(info);
        if(info.lowMemory || info.availMem < ((long)memory+384)*1024*1024) {
            report="RAM bebas belum cukup. Tutup aplikasi lain atau pilih RAM guest lebih kecil."; render(); return;
        }
        final long id=machineEcid, entropy=machineSeed;
        final Uri[] inputs=selected.clone();
        final android.content.Context app=getApplicationContext();
        nativePrepare(); busy=true; report="Menyiapkan VM..."; render();
        WORKER.execute(() -> {
            String next;
            File fixture=null;
            try {
                if(selftest) {
                    fixture=File.createTempFile("henge-board-",".img",app.getCacheDir());
                    byte[] block=new byte[512]; block[0]='H'; block[1]='E'; block[2]='N'; block[3]='G';
                    try(FileOutputStream file=new FileOutputStream(fixture)) { file.write(block); }
                    try(ParcelFileDescriptor disk=ParcelFileDescriptor.open(fixture,ParcelFileDescriptor.MODE_READ_ONLY)) {
                        next=nativeBoot(-1,disk.getFd(),-1,memory,0,0,true);
                    }
                } else {
                    try(ParcelFileDescriptor rom=app.getContentResolver().openFileDescriptor(inputs[0],"r");
                        ParcelFileDescriptor disk=app.getContentResolver().openFileDescriptor(inputs[1],"r");
                        ParcelFileDescriptor aux=app.getContentResolver().openFileDescriptor(inputs[2],"r")) {
                        if(rom==null || disk==null || aux==null) throw new java.io.IOException("File tidak tersedia");
                        next=nativeBoot(rom.getFd(),disk.getFd(),aux.getFd(),memory,id,entropy,false);
                    }
                }
            } catch(Exception e) { next="Tidak dapat membuka/menjalankan VM: "+e.getMessage(); }
            finally { if(fixture!=null && !fixture.delete()) fixture.deleteOnExit(); }
            report=next; busy=false;
        });
    }
    private void render() {
        if(output==null) return;
        test.setEnabled(!busy); boot.setEnabled(!busy); stop.setEnabled(busy);
        ram.setEnabled(!busy); ecid.setEnabled(!busy); seed.setEnabled(!busy);
        for(int i=0;i<3;i++) {
            pickers[i].setEnabled(!busy);
            if(selected[i]!=null) pickers[i].setText(getString(R.string.selected_file,new String[]{"ROM","Disk","Auxiliary"}[i],selectedNames[i]));
        }
        output.setText(busy && LIBRARY_ERROR==null ? "VM berjalan. Maksimal 30 detik.\n\n"+nativeSerial() : report);
    }
    @Override protected void onResume() { super.onResume(); handler.post(refresh); }
    @Override protected void onPause() { handler.removeCallbacks(refresh); super.onPause(); }
    @Override protected void onStop() {
        if(!isChangingConfigurations() && busy && LIBRARY_ERROR==null) nativeStop();
        super.onStop();
    }
    private Button button(LinearLayout b,String label,Runnable action) {
        Button v=new Button(this); v.setText(label); v.setAllCaps(false);
        v.setOnClickListener(w -> action.run()); b.addView(v); return v;
    }
    private EditText field(LinearLayout b,String hint,int id) {
        EditText v=new EditText(this); v.setId(id); v.setHint(hint); v.setSingleLine(true);
        v.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        b.addView(v); return v;
    }
    private TextView text(LinearLayout b,String value,int size) {
        TextView v=new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(Color.WHITE);
        v.setPadding(0,dp(8),0,dp(12)); b.addView(v); return v;
    }
    private int dp(int n) { return Math.round(n*getResources().getDisplayMetrics().density); }
}
