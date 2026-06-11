package com.example.audioplayer.ui;

import android.app.AlertDialog;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import com.example.audioplayer.AudioInfoData;
import com.example.audioplayer.FileEntryData;
import com.example.audioplayer.MainActivity;
import com.example.audioplayer.PlayerService;
import com.example.audioplayer.R;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public class BrowserFragment extends Fragment {

    private static final String ARG_DIR = "dir_path";

    private PlayerService svc;
    private String        currentDir    = "";
    private FileEntryData[] entries     = new FileEntryData[0];
    private boolean       hasParent     = false; // ".." エントリが先頭にあるか

    private TextView  tvPath;
    private ListView  listView;
    private TextView  tvRootInternal, tvRootSd;

    // ── バックグラウンドスキャン ───────────────────────────────────────────────
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final Handler         uiHandler = new Handler(Looper.getMainLooper());
    private Future<?>             pendingScan = null;

    // ─────────────────────────────────────────────────────────────────────────
    public static BrowserFragment newInstance(@Nullable String dirPath) {
        BrowserFragment f = new BrowserFragment();
        Bundle args = new Bundle();
        if (dirPath != null) args.putString(ARG_DIR, dirPath);
        f.setArguments(args);
        return f;
    }

    @Nullable @Override
    public View onCreateView(@NonNull LayoutInflater inflater,
            @Nullable ViewGroup container, @Nullable Bundle state) {
        return inflater.inflate(R.layout.fragment_browser, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View root, @Nullable Bundle state) {
        super.onViewCreated(root, state);

        tvPath         = root.findViewById(R.id.tv_current_path);
        listView       = root.findViewById(R.id.list_view);
        tvRootInternal = root.findViewById(R.id.btn_root_internal);
        tvRootSd       = root.findViewById(R.id.btn_root_sd);

        if (getActivity() instanceof MainActivity)
            svc = ((MainActivity) getActivity()).service;
        if (svc == null) return;

        // ストレージルートボタン
        final String[] roots = svc.bridge.nativeGetStorageRoots();
        if (roots != null && roots.length > 0) {
            tvRootInternal.setText(R.string.internal_storage);
            tvRootInternal.setOnClickListener(v -> navigateTo(roots[0]));
            tvRootInternal.setVisibility(View.VISIBLE);
            if (roots.length > 1) {
                tvRootSd.setText(R.string.sd_card);
                tvRootSd.setOnClickListener(v -> navigateTo(roots[1]));
                tvRootSd.setVisibility(View.VISIBLE);
            }
        }

        // 初期ディレクトリへ移動
        final String argDir = getArguments() != null
                ? getArguments().getString(ARG_DIR) : null;
        if (argDir != null) {
            navigateTo(argDir);
        } else if (roots != null && roots.length > 0) {
            navigateTo(roots[0]);
        }
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        if (pendingScan != null) pendingScan.cancel(true);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        executor.shutdownNow();
    }

    // ─────────────────────────────────────────────────────────────────────────
    private void navigateTo(final String dirPath) {
        if (svc == null || dirPath == null) return;
        currentDir = dirPath;
        tvPath.setText(dirPath);

        // ── ".." の要否判断 ────────────────────────────────────────────────────
        // sep > 0 のときのみ ".." を追加。
        //   例: "/sdcard/Music" → sep=7  → 追加
        //   例: "/sdcard"       → sep=0  → 追加しない（ルートのため）
        //   ※ sep == 0 のとき labels.add(null) していたのが即死 NPE の原因
        final int sep = dirPath.lastIndexOf('/');
        hasParent = (sep > 0);

        // 前のスキャンをキャンセル
        if (pendingScan != null) pendingScan.cancel(true);

        // ── バックグラウンドでスキャン（UI スレッドのブロック / ANR を防ぐ）──────
        pendingScan = executor.submit(() -> {
            final FileEntryData[] scanned = svc.bridge.nativeScanDirectory(dirPath);
            uiHandler.post(() -> {
                if (!isAdded() || getView() == null) return;     // デタッチ済みなら無視
                if (!dirPath.equals(currentDir))    return;     // 古い結果なら無視

                entries = (scanned != null) ? scanned : new FileEntryData[0];
                bindList(dirPath, sep);
            });
        });
    }

    // ─────────────────────────────────────────────────────────────────────────
    private void bindList(final String dirPath, final int sep) {
        final List<String> labels = new ArrayList<>();
        final List<String> subs   = new ArrayList<>();

        // ".." エントリ: null は絶対に入れない
        if (hasParent) {
            labels.add("📂  ..");
            subs.add("上の階層");
        }
        // 通常エントリ
        for (FileEntryData fe : entries) {
            if (fe.isDirectory) {
                labels.add("📂  " + fe.name);
                subs.add("");
            } else {
                labels.add("🎵  " + fe.name);
                final String sub = fe.getSubLabel();
                subs.add(sub != null ? sub : "");
            }
        }

        // ── アダプター ─────────────────────────────────────────────────────────
        // labels に null が入らないことを保証しているので getText(item) で NPE は起きない
        final ArrayAdapter<String> adapter = new ArrayAdapter<String>(
                requireContext(), R.layout.item_file, R.id.item_name, labels) {
            @NonNull @Override
            public View getView(int pos, @Nullable View convertView,
                    @NonNull ViewGroup parent) {
                final View v = super.getView(pos, convertView, parent);
                final TextView sub = v.findViewById(R.id.item_sub);
                if (sub != null && pos < subs.size()) {
                    final String s = subs.get(pos);
                    sub.setText(s != null ? s : "");
                }
                return v;
            }
        };
        listView.setAdapter(adapter);

        // ── タップ: ディレクトリ → 移動 / ファイル → 再生 ─────────────────────
        listView.setOnItemClickListener((parent, view, pos, id) -> {
            if (hasParent && pos == 0) {
                // ".." — 1 階層上へ
                navigateTo(dirPath.substring(0, sep));
                return;
            }
            final int idx = pos - (hasParent ? 1 : 0);
            if (idx < 0 || idx >= entries.length) return;
            final FileEntryData fe = entries[idx];
            if (fe.isDirectory) {
                if (getActivity() instanceof MainActivity)
                    ((MainActivity) getActivity()).showBrowser(fe.fullPath);
            } else {
                openAndPlay(fe.fullPath);
            }
        });

        // ── 長押し: 詳細ダイアログ ─────────────────────────────────────────────
        listView.setOnItemLongClickListener((parent, view, pos, id) -> {
            final int idx = pos - (hasParent ? 1 : 0);
            if (idx < 0 || idx >= entries.length || entries[idx].isDirectory)
                return false;
            showDetailDialog(entries[idx]);
            return true;
        });
    }

    // ─────────────────────────────────────────────────────────────────────────
    private void openAndPlay(String path) {
        if (svc == null) return;
        if (svc.bridge.nativeLoadFile(svc.engineHandle, path)) {
            svc.bridge.nativePlay(svc.engineHandle);
            if (getActivity() instanceof MainActivity)
                ((MainActivity) getActivity()).showPlayer();
        }
    }

    private void showDetailDialog(FileEntryData fe) {
        if (!isAdded()) return;
        final AudioInfoData info = fe.audioInfo != null
                ? fe.audioInfo
                : (svc != null ? svc.bridge.nativeReadMetadata(fe.fullPath) : null);
        if (info == null) return;

        final StringBuilder sb = new StringBuilder();
        sb.append("フォーマット: ").append(info.getFormatName()).append("\n");
        if (info.isLossless) {
            sb.append("サンプルレート: ").append(info.getSampleRateLabel()).append("\n");
            sb.append("ビット深度: ").append(info.bitDepth).append("bit\n");
        } else {
            sb.append("ビットレート: ").append(info.bitrateKbps).append(" kbps\n");
        }
        sb.append("時間: ").append(info.getDurationLabel()).append("\n");
        sb.append("サイズ: ").append(info.getFileSizeLabel());
        if (info.isMqa) {
            sb.append("\n\nMQA: ").append(info.getMqaBadgeText());
        }
        if (!info.title.isEmpty())  sb.append("\nタイトル: ").append(info.title);
        if (!info.artist.isEmpty()) sb.append("\nアーティスト: ").append(info.artist);
        if (!info.album.isEmpty())  sb.append("\nアルバム: ").append(info.album);

        new AlertDialog.Builder(requireContext())
                .setTitle(fe.name)
                .setMessage(sb.toString())
                .setPositiveButton("再生", (d, w) -> openAndPlay(fe.fullPath))
                .setNegativeButton("閉じる", null)
                .show();
    }
}
