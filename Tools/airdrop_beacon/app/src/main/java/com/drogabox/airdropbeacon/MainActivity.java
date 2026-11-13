/*
 * AirDrop Beacon — sends the BLE advertisement an Apple device sends while its
 * share sheet looks for AirDrop receivers: manufacturer data 0x004C, message
 * type 0x05, length 0x12: 8 zero bytes, version 0x01, four 16-bit hashes (the
 * high bits of SHA-256 of the sender's e-mail addresses and phone numbers) and
 * a zero byte (Docs/AWDL_REFERENCIAS_EXTERNAS.md §12.2). A Mac whose AirDrop
 * accepts everyone does not check the hashes. Used to trigger the Mac's
 * sharingd without an iPhone.
 *
 * From adb:
 *   adb shell pm grant com.drogabox.airdropbeacon android.permission.BLUETOOTH_ADVERTISE
 *   adb shell am start -n com.drogabox.airdropbeacon/.MainActivity --ez start true [--es ids a@b.c,+15551234]
 *   adb shell am start -n com.drogabox.airdropbeacon/.MainActivity --ez stop true
 *   adb logcat -s AirDropBeacon
 *
 * Copyright (c) 2026 DrogaBox — GPL-2.0
 */
package com.drogabox.airdropbeacon;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertisingSet;
import android.bluetooth.le.AdvertisingSetCallback;
import android.bluetooth.le.AdvertisingSetParameters;
import android.bluetooth.le.BluetoothLeAdvertiser;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

public class MainActivity extends Activity {
    private static final String TAG = "AirDropBeacon";
    private static final int APPLE = 0x004C;
    private static final int TYPE_AIRDROP = 0x05;
    private static final int REQ_ADVERTISE = 1;

    private BluetoothLeAdvertiser advertiser;
    private boolean advertising;
    private String pendingIds;
    private EditText idsField;
    private TextView status;
    private Button toggle;

    /* A legacy, non-connectable, non-scannable set: ADV_NONCONN_IND, as Apple
     * sends it (Stute et al., USENIX Security 2019, §4.1). */
    private final AdvertisingSetCallback callback = new AdvertisingSetCallback() {
        @Override
        public void onAdvertisingSetStarted(AdvertisingSet set, int txPower, int status) {
            advertising = status == AdvertisingSetCallback.ADVERTISE_SUCCESS;
            show(advertising ? "advertising (ADV_NONCONN_IND, tx " + txPower + " dBm)"
                             : "advertising failed: " + status);
        }

        @Override
        public void onAdvertisingSetStopped(AdvertisingSet set) {
            advertising = false;
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        root.setPadding(pad, pad * 3, pad, pad);
        idsField = new EditText(this);
        idsField.setHint("e-mails / phones, comma separated (optional)");
        idsField.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        toggle = new Button(this);
        toggle.setText("Start");
        toggle.setOnClickListener(v -> {
            if (advertising) {
                stop();
            } else {
                start(idsField.getText().toString());
            }
        });
        status = new TextView(this);
        status.setTextIsSelectable(true);
        root.addView(idsField);
        root.addView(toggle);
        root.addView(status);
        setContentView(root);
        handle(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handle(intent);
    }

    @Override
    protected void onDestroy() {
        stop();
        super.onDestroy();
    }

    private void handle(Intent intent) {
        if (intent == null) {
            return;
        }
        if (intent.getBooleanExtra("stop", false)) {
            stop();
        } else if (intent.getBooleanExtra("start", false)) {
            String ids = intent.getStringExtra("ids");
            if (ids != null) {
                idsField.setText(ids);
            }
            start(ids == null ? "" : ids);
        }
    }

    /** The message body after the type and length bytes. */
    static byte[] airdropBody(String ids) {
        byte[] b = new byte[18];
        b[8] = 0x01;
        int slot = 0;
        for (String raw : ids.split(",")) {
            String id = raw.trim();
            if (id.isEmpty() || slot == 4) {
                continue;
            }
            byte[] h = sha256(id);
            b[9 + 2 * slot] = h[0];
            b[10 + 2 * slot] = h[1];
            slot++;
        }
        return b;
    }

    private static byte[] sha256(String s) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(s.getBytes(StandardCharsets.UTF_8));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) {
            sb.append(String.format("%02x", x & 0xff));
        }
        return sb.toString();
    }

    private void start(String ids) {
        if (checkSelfPermission(Manifest.permission.BLUETOOTH_ADVERTISE)
                != PackageManager.PERMISSION_GRANTED) {
            pendingIds = ids;
            requestPermissions(new String[] {Manifest.permission.BLUETOOTH_ADVERTISE}, REQ_ADVERTISE);
            return;
        }
        BluetoothManager bm = getSystemService(BluetoothManager.class);
        BluetoothAdapter adapter = bm == null ? null : bm.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            show("Bluetooth is off");
            return;
        }
        advertiser = adapter.getBluetoothLeAdvertiser();
        if (advertiser == null) {
            show("no BLE advertiser on this device");
            return;
        }
        if (advertising) {
            advertiser.stopAdvertisingSet(callback);
        }
        byte[] body = airdropBody(ids);
        byte[] msg = new byte[2 + body.length];
        msg[0] = TYPE_AIRDROP;
        msg[1] = (byte) body.length;
        System.arraycopy(body, 0, msg, 2, body.length);
        AdvertisingSetParameters.Builder pb = new AdvertisingSetParameters.Builder()
                .setLegacyMode(true)
                .setConnectable(false)
                .setScannable(false)
                .setInterval(AdvertisingSetParameters.INTERVAL_MIN)
                .setTxPowerLevel(AdvertisingSetParameters.TX_POWER_HIGH);
        /* Apple's advert starts with a Flags structure; Android adds one only
         * to discoverable sets (API 34+). */
        if (Build.VERSION.SDK_INT >= 34) {
            pb.setDiscoverable(true);
        }
        AdvertisingSetParameters params = pb.build();
        AdvertiseData data = new AdvertiseData.Builder()
                .setIncludeDeviceName(false)
                .setIncludeTxPowerLevel(false)
                .addManufacturerData(APPLE, msg)
                .build();
        Log.i(TAG, "start: 004c " + hex(msg));
        status.setText("004c " + hex(msg));
        advertiser.startAdvertisingSet(params, data, null, null, null, callback);
    }

    private void stop() {
        if (advertiser != null && advertising) {
            advertiser.stopAdvertisingSet(callback);
        }
        advertising = false;
        show("stopped");
    }

    private void show(String s) {
        Log.i(TAG, s);
        runOnUiThread(() -> {
            status.append("\n" + s);
            toggle.setText(advertising ? "Stop" : "Start");
        });
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        if (code == REQ_ADVERTISE && results.length > 0
                && results[0] == PackageManager.PERMISSION_GRANTED) {
            start(pendingIds == null ? "" : pendingIds);
        } else {
            show("BLUETOOTH_ADVERTISE denied");
        }
    }
}
