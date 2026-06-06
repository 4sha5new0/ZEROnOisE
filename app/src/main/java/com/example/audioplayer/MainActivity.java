package com.example.audioplayer;

import android.Manifest;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.IBinder;
import android.widget.Toast;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import com.example.audioplayer.ui.BrowserFragment;
import com.example.audioplayer.ui.PlayerFragment;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_READ_STORAGE = 100;

    // サービスとのバインド
    public  PlayerService service;
    private boolean       bound = false;

    private final ServiceConnection connection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder iBinder) {
            service = ((PlayerService.LocalBinder) iBinder).getService();
            bound   = true;
            // 初回起動時はブラウザを表示
            if (getSupportFragmentManager().findFragmentById(R.id.fragment_container) == null) {
                showBrowser(null);
            }
        }
        @Override
        public void onServiceDisconnected(ComponentName name) {
            bound = false;
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        if (ContextCompat.checkSelfPermission(this,
                Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this,
                new String[]{Manifest.permission.READ_EXTERNAL_STORAGE},
                REQUEST_READ_STORAGE);
        } else {
            bindAndStartService();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
            @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_READ_STORAGE) {
            if (grantResults.length > 0
                    && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                bindAndStartService();
            } else {
                Toast.makeText(this, R.string.permission_denied, Toast.LENGTH_LONG).show();
                finish();
            }
        }
    }

    private void bindAndStartService() {
        final Intent intent = new Intent(this, PlayerService.class);
        startService(intent);
        bindService(intent, connection, BIND_AUTO_CREATE);
    }

    /** ファイルブラウザを表示（rootPath=null のときはストレージルート） */
    public void showBrowser(String dirPath) {
        final BrowserFragment frag = BrowserFragment.newInstance(dirPath);
        getSupportFragmentManager().beginTransaction()
            .replace(R.id.fragment_container, frag)
            .addToBackStack(dirPath)
            .commit();
    }

    /** 再生画面を表示 */
    public void showPlayer() {
        // スタックに既存の PlayerFragment があれば pop して戻る
        final int stackSize = getSupportFragmentManager().getBackStackEntryCount();
        for (int i = 0; i < stackSize; i++) {
            if ("player".equals(getSupportFragmentManager()
                    .getBackStackEntryAt(i).getName())) {
                getSupportFragmentManager().popBackStack("player",
                    androidx.fragment.app.FragmentManager.POP_BACK_STACK_INCLUSIVE);
                break;
            }
        }
        getSupportFragmentManager().beginTransaction()
            .replace(R.id.fragment_container, new PlayerFragment())
            .addToBackStack("player")
            .commit();
    }

    @Override
    protected void onDestroy() {
        if (bound) { unbindService(connection); bound = false; }
        super.onDestroy();
    }
}
